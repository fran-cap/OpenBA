// ============================================================================
// src/engine/audio/audio.cpp — Wave 8, lane C — Audio::Scene / ::Configuration
// ============================================================================
// Ports 16 of the 21 Audio exports our tree calls (Wave 8 landed 13; the
// Wave 8.5 close-out added Pause, StopEffect3D and StopAll, plus the two
// unexported-by-our-tree siblings PauseEffect and StopEffect they call). Every retail body below was
// decompiled AND disassembled at its real address in the SHIPPED
// Runtime-x64.dll (image base 0x180000000, read 2026-09-07); addresses and the
// oracle shape for each function are in docs/history/runtime_port/WAVE8_LANE_C.md.
//
// STILL HELD BACK, with reasons (docs/history/runtime_port/WAVE8_LANE_C.md has Wave
// 8's argument; PLAN §6: a held-back function with a stated reason is a
// result). THREE, down from seven — see the Wave 8.5 close-out block further
// down this file for why the other four moved and what each of their stated
// blockers turned out to be worth:
//
//   ?Update@Scene@Audio@@…             0x1800df6c0 — ~150 decompiled lines
//       spatialising every live emitter against Device::PlayPlatform, mixing
//       in System::Random state and calling back into Device's channel
//       objects. The synthetic-scene technique that unblocked the teardown
//       family does NOT reach this one: putting the device out of reach still
//       leaves the RNG, and a replay that draws twice from System::Random
//       diverges by construction. What it needs is a snapshot/restore of the
//       Random state as well, which is a real and bounded next step rather
//       than a missing tool. It does NOT need SoundBuffer/SoundEffect PORTS —
//       Wave 8 said it did; what it needs from them is LAYOUT, and a faithful
//       body may call them by address exactly as the teardown family calls
//       Device::Stop.
//   ?Update@Device@Audio@@…            0x1800de520 — Device internals
//       (platform channel completion callbacks). Here the device IS the
//       subject, not an unreachable callee, so no synthetic Scene helps.
//   ?FreeChannelPlatform@Device@Audio@@…ord2380 (0x1800de6f0) — PRIVATE
//       member, COM vtable call at (**channel)+0x90, critical section at
//       Device+0xa50. Same class as Device::Update, and for the same reason.
//
// PORTED SINCE, and the blockers that did not survive being re-read (each was
// correct on the evidence its lane had — WAVE8_5.md §10 is about transport,
// not judgement):
//
//   ?Serialize@Scene@Audio@@…          0x1800e0050 — held back because "the
//       profile build's IO::Encoding vtable offsets do not match retail's and
//       this lane did not re-derive retail's own". Wave 4 had already written
//       retail's offsets into src/engine/io/rt_io_port.cpp, where they had
//       sat since. Ported in Wave 8.5, 32,772 identical emissions.
//   ?StopEffect3D@ / ?Pause@ / ?StopAll@Scene@Audio@@ — held back because
//       they "reach Audio::Device::Stop, an audible non-replayable side
//       effect, and therefore have no oracle either way". True of a LIVE
//       scene and false in general: Device::Stop is reachable only through a
//       non-null Channel::platformChannel[b], which is a SCENE field, so a
//       synthetic scene removes the device by construction. Ported in Wave
//       8.5's close-out with an exhaustive side-by-side probe.
//
// `?_singleton@Configuration@Audio@@0PEAV12@EA` (ord 4801) is a DATA export,
// not a function — nothing to register; engine_funcs.cpp already resolves it
// (g_pAudioConfigSingleton) exactly as WAVE3/4 record for other DLL globals.
//
// THE PHANTOM NAME. `?PlayEffect@Scene@Audio@@QEAIV?$ExternalPtr@VSoundEffect
// @Audio@@@Object@@W4SoundType@12@VTime@System@@@Z` (spelled QEAI, not QEAA)
// does not exist in the shipped DLL — one of only 3 phantoms left in the
// whole tree (tools/export_name_audit.py). engine_funcs.cpp's
// `fn_Audio_Scene_PlayEffect` already has its own
// `if (!fn) fn = ResolveAddr(Module::Runtime, 0x1800def60)` fallback, so this
// was NEVER a functional gap — the real, correctly-spelled export
// (`?PlayEffect@Scene@Audio@@QEAAIV?$ExternalPtr@VSoundEffect@Audio@@@Object@
// @W4SoundType@12@AEBVTime@System@@@Z`, retail 0x1800def60, ord 3423) is what
// every call actually reached. Fixed at its source in engine_funcs.cpp (the
// ResolveExport string, not the working fallback) rather than left as a live
// landmine for the next person who trusts the string over the behaviour.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include "audio/Audio.h"
#include "global_types.h"   // Vector3
#include "io/Serializer.h"        // Stream / Serializer / rw_vtbl (Wave 4)
#include "io/Stream.h"            // IO::Stream::_direction
#include "io/Persistence.h"       // ENDGAME B.3.9 SS1: Persistence::_singleton
#include "system/timer_internal.h"                  // Wave 9 lane W: TimerSecondsPerTick
#include "../collection/array_internal.h"     // ENDGAME B.3.2 step 2: kSpec_EffectSlot bridge
#include "audio/Device.h"
#include "audio/SoundEffect.h"
#include "system/Value.h"         // System::Time
#include "audio/audio_cvtt.h"     // Audio::CvttSd2Si64 (review F-136)

// The EffectSlot bulk serialize, this directory's own body at 0x180052640
// (audio_effectslot_serialize.cpp). Called directly (B.3.7).
extern "C" void RtAudio_Scene_SerializeEffectSlots(IO::Serializer* ser, int32_t count,
                                                   Audio::Scene::AttenutedEffect* data);

// ----------------------------------------------------------------------------
// ResolveAddr / Module, forward-declared rather than pulled in from
// standalone/resolve.h — same reason input.cpp does it (windows.h leaks
// engine-colliding macros). Must stay byte-identical to resolve.h.

using RtAudio::Scene;
using RtAudio::Configuration;
using RtAudio::Channel;
using RtAudio::EffectSlot;
using RtAudio::EmitterInstance;

namespace {

// ----------------------------------------------------------------------------
// Dependencies out of this lane's scope.
//
// WAVE 9 LANE V2, 2026-09-07: these two were the raw DLL door, with the reason
// "never through ResolveExport, or a diff would compare ours to ours". That
// reason is right for an ORACLE and wrong for these: nothing diffs MemAlloc /
// MemFree here, they are the allocation the ported bodies actually perform, so
// they are a REAL DEPENDENCY and belong on the port door. The raw door is
// the raw DLL door, which returns NULL the moment Runtime-x64.dll is absent —
// i.e. a null call at cutover, not a degradation (rawdoor_audit.py).
//
// RT_EXPORT_ONCE keeps the one-resolve-per-call-site caching that made
// the raw DLL door attractive here in the first place
// (reference_rtoriginal_is_getprocaddress.md) and routes through the registry,
// so when `RT_ALLOC` arms the allocator port these allocate on the
// same heap as the ~20 `code/` sites that already resolve these two names with
// ResolveExport. A heap cannot be half-ours (WAVE9.md sec3).
// ----------------------------------------------------------------------------
using MemAllocFn = void* (*)(uint64_t size, int align);
using MemFreeFn  = void  (*)(void* p);

//
// ENDGAME B.3.7 block B4: both now go through `rtcoll::MemAlloc` /
// `rtcoll::MemFree`, the one allocator door every Collection::Array body uses
// (Collection.h's allocator banner). Scene's destructor is the IMPLICIT one
// now (src/engine/audio/Audio.h), i.e. `~Array`, which frees through that
// door — so the buffers the ctor door and AddEmitter allocate must come from
// it too, or the heap is half-shimmed. Same allocator on a normal run.
void* MemAlloc(uint64_t size, int align) { return rtcoll::MemAlloc(size, align); }
void MemFree(void* p) { rtcoll::MemFree(p); }

// System::Timer::_secondsPerTick and Time helpers are not needed by anything
// in this file (Scene::Update, their only consumer, lives in audio_scene_update.cpp).

// ----------------------------------------------------------------------------
// Collection::Array<T>::SetSize(n, fillValue) — the growth policy documented
// in src/engine/collection/Collection.h's Array<T> comment, transcribed
// here rather than added to that header (this lane does not own it — see
// WAVE8.md's cross-lane ownership contract). Independently re-derived from
// THREE retail bodies that all agree (FUN_180051020 hardcoded-100,
// FUN_180051390 hardcoded-0x1000, FUN_180051aa0 fully generic — the one used
// by AddEmitter, which is why the fill-value parameter exists at all: it
// doubles as push_back(value) via SetSize(_size + 1, &value)).
//
// Simplified relative to retail in ONE way, stated rather than hidden: retail
// destroys/copy-constructs element-by-element to support non-POD T; every T
// this lane instantiates (Channel, EffectSlot, EmitterInstance) is a plain
// struct of ints/floats/pointers with no owning sub-object of its own EXCEPT
// EffectSlot::positions, which is handled explicitly by the caller (Scene's
// ctor/dtor), never through this helper. A memcpy of the live range is
// therefore observably identical to retail's element-wise copy for every T
// actually used here.
// ----------------------------------------------------------------------------
template <class T>
void ArraySetSize(Collection::Array<T>* arr, int32_t n, const T& fillValue) {
    int32_t oldSize = arr->_size;
    if (n == oldSize) return;
    T* oldData = arr->_data;
    arr->_size = n;
    bool grew = false;
    if (arr->_capacity < n) {
        arr->_capacity = n;
        if (n == oldSize + 1 && oldSize != 0) arr->_capacity = n * 3;
        arr->_data = static_cast<T*>(MemAlloc(sizeof(T) * (uint64_t)arr->_capacity, (int)alignof(T)));
        grew = true;
    }
    // ENDGAME B.3.2 closing pass: both copies are spelled as byte copies through
    // a `void*` destination. They already WERE byte copies — the banner above
    // says so, and `EffectSlot`'s implicit `operator=` was a memberwise copy of
    // PODs plus `Array`'s then-shallow one — but `Collection::Array<T>::operator=`
    // is now retail's DEEP assign, so leaving `arr->_data[i] = fillValue` as an
    // assignment would run `ArrayAssignCore` over `EffectSlot::positions` in a
    // buffer that MemAlloc just handed back uninitialised, i.e. free a garbage
    // pointer. Byte-identical to what this helper did before, and the `void*`
    // cast is also what silences -Wclass-memaccess on the line above.
    if (grew && oldSize > 0)
        std::memcpy(static_cast<void*>(arr->_data), static_cast<const void*>(oldData),
                    sizeof(T) * (size_t)oldSize);
    for (int32_t i = oldSize; i < n; ++i)
        std::memcpy(static_cast<void*>(&arr->_data[i]), static_cast<const void*>(&fillValue),
                    sizeof(T));
    if (grew && oldData)
        MemFree(oldData);
}

// EffectSlot lookup by id: index = (id>>1)&0xfff, validated against the
// stored idTag with the top-19-bit mask. Shared by every EffectSlot-table
// accessor (AddEmitter, MoveEmitter, RemoveEmitter, SetEmitterVolume,
// GetEmitterCount all repeat this exact check in retail).
EffectSlot* FindEffectSlot(const Scene* self, uint32_t id) {
    uint32_t idx = (id >> Scene::kHandleIndexShift) & Scene::kEffectIndexMask;
    EffectSlot* slot = &self->effects._data[idx];
    if (((slot->idTag ^ id) & Scene::kEffectTagMask) != 0) return nullptr;
    return slot;
}

}  // namespace

// ============================================================================
// Audio::Scene::Scene() — retail 0x1800e0160 (`??0Scene@Audio@@QEAA@XZ`).
// Retail: [+0x00] = 0 (qword), [+0x08] = 1 (dword), the two Array headers
// zeroed in declaration order (+0x10/+0x18, +0x20/+0x28), then
// FUN_180051390(this+0x20, 0x1000) (the AttenutedEffect table), then
// FUN_180051020(this+0x10) (the 256 Channels), then [+0x30] dword 0 and
// [+0x34] byte 0 (the five `paused` bytes). Its EH frame (-2 at [RSP+0x20])
// is the two Array members' destructors. The member initialisers below are
// those stores in that order; +0x0c is not written, as in retail.
// ============================================================================
namespace Audio {

Scene::Scene() : time(0), playGeneration(1), channels(), effects() {
    EffectSlot defaultSlot{};
    defaultSlot.baseVolume = 1.0f;
    ArraySetSize(&effects, kEffectSlotCount, defaultSlot);   // matches FUN_180051390(this+0x20, 0x1000)

    Channel defaultChannel{};
    defaultChannel.volume = 1.0f;
    defaultChannel.sampleVolume = 1.0f;
    ArraySetSize(&channels, kChannelCount, defaultChannel); // matches FUN_180051020(this+0x10)

    for (int i = 0; i < 5; ++i) paused[i] = 0;
}

}  // namespace Audio

// The in-place door the exe's two raw-storage holders still call
// (GameUpdateLoop::Load's MemAlloc(0x38,8), MenuDialogLoopT +0xb30): it runs
// the constructor above. Global placement new: the class's own placement
// `operator new` (0x180050920) is declared, not ported.
Scene* RtAudio_Scene_Ctor(Scene* self) {
    return ::new (static_cast<void*>(self)) Scene();
}

// ============================================================================
// Audio::Scene::~Scene() — retail 0x1800e0120. Frees the EffectSlot table's
// owned per-slot `positions` buffers first (FUN_180052110), then both outer
// buffers, matching retail's order (+0x20 torn down before +0x10).
// ============================================================================
// B.3.7: the IMPLICIT destructor IS retail's body (Audio.h banner):
// ~Array<AttenutedEffect> (each slot's `positions`, then the buffer, +0x20)
// then ~Array<Channel> (+0x10), reverse declaration order. B.4 wave X: the
// one caller (GameUpdateLoop::Shutdown) runs it by name; the void* forwarder
// RtAudio_Scene_Dtor is gone.

// ============================================================================
// void Audio::Scene::AddEmitter(uint id, const Vector3& pos, float volume)
// retail 0x1800dedf0. Mutates the live EffectSlot::positions array — SHADOW
// oracle (verified against the DLL's own read-only GetEmitterCount on the
// SAME slot afterward, since both implementations share one Scene object).
// ============================================================================
namespace Audio {

void Scene::AddEmitter(uint32_t id, const Vector3& pos, float volume) {
    EffectSlot* slot = FindEffectSlot(this, id);
    if (slot) {
        EmitterInstance inst{pos, volume};
        ArraySetSize(&slot->positions, slot->positions._size + 1, inst);
    }

}

// ============================================================================
// void Audio::Scene::MoveEmitter(uint id, const Vector3& oldPos, const Vector3& newPos)
// retail 0x1800dec40 — finds the FIRST instance whose position exactly equals
// oldPos and overwrites its xyz (volume untouched). SHADOW oracle.
// ============================================================================
void Scene::MoveEmitter(uint32_t id, const Vector3& oldPos, const Vector3& newPos) {
    EffectSlot* slot = FindEffectSlot(this, id);
    if (slot) {
        for (int32_t i = 0; i < slot->positions._size; ++i) {
            EmitterInstance& e = slot->positions._data[i];
            if (e.position.x == oldPos.x && e.position.y == oldPos.y && e.position.z == oldPos.z) {
                e.position.x = newPos.x; e.position.y = newPos.y; e.position.z = newPos.z;
                break;
            }
        }
    }
}

// ============================================================================
// void Audio::Scene::RemoveEmitter(uint id, const Vector3& pos)
// retail 0x1800decf0 — removes every instance whose position exactly equals
// pos (retail's inner shift loop re-checks the index it just compacted into,
// so a duplicate position is fully removed in one call). SHADOW oracle.
// ============================================================================
void Scene::RemoveEmitter(uint32_t id, const Vector3& pos) {
    EffectSlot* slot = FindEffectSlot(this, id);
    if (slot) {
        int32_t i = 0;
        while (i < slot->positions._size) {
            EmitterInstance& e = slot->positions._data[i];
            if (e.position.x == pos.x && e.position.y == pos.y && e.position.z == pos.z) {
                for (int32_t j = i; j + 1 < slot->positions._size; ++j)
                    slot->positions._data[j] = slot->positions._data[j + 1];
                --slot->positions._size;
                // do not advance i: the slid-down element must be re-checked
            } else {
                ++i;
            }
        }
    }

}

// ============================================================================
// void Audio::Scene::SetEmitterVolume(uint id, const Vector3& pos, float volume)
// retail 0x1800ded60 — finds the FIRST instance whose position exactly equals
// pos and overwrites its volume. SHADOW oracle.
// ============================================================================
void Scene::SetEmitterVolume(uint32_t id, const Vector3& pos, float volume) {
    EffectSlot* slot = FindEffectSlot(this, id);
    if (slot) {
        for (int32_t i = 0; i < slot->positions._size; ++i) {
            EmitterInstance& e = slot->positions._data[i];
            if (e.position.x == pos.x && e.position.y == pos.y && e.position.z == pos.z) {
                e.volume = volume;
                break;
            }
        }
    }
}

// ============================================================================
// int Audio::Scene::GetEmitterCount(uint id) const — retail 0x1800dec00.
// Pure read. Full replay oracle.
// ============================================================================
int32_t Scene::GetEmitterCount(uint32_t id) const {
    const EffectSlot* slot = FindEffectSlot(this, id);
    int ours = slot ? slot->positions._size : 0;
    return ours;
}

// ============================================================================
// bool Audio::Scene::IsEffectPlaying(uint handle) const — retail 0x1800df680.
// Pure read of the Channel table (a DIFFERENT table/handle space than the
// EffectSlot functions above — see the header note). Full replay oracle.
// ============================================================================
bool Scene::IsEffectPlaying(uint32_t handle) const {
    uint32_t idx = (handle >> kHandleIndexShift) & kChannelIndexMask;
    bool ours = false;
    if ((int32_t)idx < channels._size)
        ours = ((channels._data[idx].handleTag ^ handle) & kChannelTagMask) == 0;
    return ours;
}

// ============================================================================
// uint Audio::Scene::FindEffect3D(ExternalPtr<SoundEffect> soundRef) const
// retail 0x1800dee70. Pure linear scan of the 4096-slot EffectSlot table for
// a matching soundEffectRef. Full replay oracle.
// ============================================================================
uint32_t Scene::FindEffect3D(Object::ExternalPtr<SoundEffect> effect) {
    uint32_t ours = 0;
    for (int32_t i = 0; i < effects._size; ++i) {
        if (effects._data[i].soundEffectRef == effect) {
            ours = effects._data[i].idTag;
            break;
        }
    }
    return ours;
}

// ============================================================================
// uint Audio::Scene::PlayEffect(ExternalPtr<SoundEffect> soundRef,
//                                Scene::SoundType type, const Time& time)
// retail 0x1800def60 (the REAL, QEAA-spelled export — see the phantom-name
// note at the top of this file). Allocates a free Channel slot; no Device
// call happens here (that is Scene::Update, audio_scene_update.cpp), so the only side
// effect is on Scene-owned state. Consuming a channel slot is a real mutation
// — calling both implementations would consume two — so SHADOW, verified via
// the DLL's own read-only IsEffectPlaying on the handle we just minted.
// ============================================================================
uint32_t Scene::PlayEffect(Object::ExternalPtr<SoundEffect> effect, SoundType soundType,
                           const System::Time& time) {
    uint32_t handle = 0;
    if (effect) {
        int32_t free = -1;
        for (int32_t i = 0; i < channels._size && i < kChannelCount; ++i) {
            if (channels._data[i].handleTag == 0) { free = i; break; }
        }
        if (free >= 0) {
            Channel& ch = channels._data[free];
            ch.soundEffectRef = effect;
            ch.soundType = (uint32_t)soundType;
            ch.startTime = time;
            playGeneration += 1;
            handle = ((uint32_t)free << kHandleIndexShift)
                     | ((uint32_t)playGeneration << kChannelGenerationShift);
            ch.handleTag = handle;
        }
    }

    return handle;
}

// ============================================================================
// int Audio::Scene::FindFree3DEffect(ExternalPtr<SoundEffect>, const Time&,
//                                    const Vector3&) const
// retail 0x1800deae0, ordinal 2352 — Wave 9 lane W.
//
// WHY IT IS PORTED NOW, AND WHY THE HOLDBACK REASON BELOW IT DID NOT SURVIVE
// READING THE BODY. PlayEffect3D's comment (immediately below) says this helper
// "dereferences Audio::SoundEffect internals (a class this lane does not own)"
// and calls into retail for that one sub-decision. Wave 9 lane V then read the
// row as REAL DEPENDENCY (MIGRATED): the door moved to RT_EXPORT_ONCE, but with
// no port and no mint entry `ResolveExport` falls through to
// `the raw DLL door` and returns null, so at cutover PlayEffect3D calls
// through null on the first positional sound the game plays.
//
// The stated blocker is smaller than it sounds, which is the shape
// reference_holdback_blockers_are_stale_check_them_first.md is about. The
// SoundEffect "internals" this function touches are exactly TWO scalars:
//
//     +0x20  float    the 3D cutoff radius   (MOVSS XMM3,[RDX+0x20])
//     +0x28  int64_t  the reuse window, divided by 20.0f  (MOV RAX,[RDX+0x28])
//
// Two field offsets are not a class layout, and nothing else about SoundEffect
// is read, written, or dispatched through. There is no vtable call, no
// allocation, and no store of any kind: this is a PURE CONST READER over the
// Scene's own 4096-entry effect table, which is why it gets a FULL call-both
// oracle rather than a shadow. Same shape as
// reference_const_reader_needs_a_caller_not_a_human.md.
//
// TRANSCRIBED FROM THE DISASSEMBLY, not the decompiler's C, for three reasons
// the C got wrong or would have got wrong
// (reference_ghidra_c_output_vs_disassembly.md):
//
//  * Ghidra prints the free-slot test as `pfVar2[9] == 0.0`, a FLOAT compare of
//    +0x24. The instruction is `CMP dword ptr [RCX+0x24],0` — an INTEGER
//    compare of `EffectSlot::idTag`. A float compare there would read
//    0x80000000 (a valid tag) as -0.0f and equal to zero.
//  * The distance sum's ASSOCIATION is observable in float: retail computes
//    `(dy*dy + dx*dx) + dz*dz`, in that order (XMM2 holds dy², XMM1 dx², XMM0
//    dz²; ADDSS XMM2,XMM1 then ADDSS XMM2,XMM0). Any other grouping can differ
//    in the last bit and flip the radius test at the boundary.
//  * Both COMISS tests are spelled so the NaN case matches the FLAGS, not C's
//    default. `JNC` after `COMISS d2,rr` takes the body when CF==1, which is
//    `d2 < rr` OR UNORDERED — hence `!(d2 >= rr)` rather than `d2 < rr`. The
//    earlier `JBE` takes the body only when ordered-and-strictly-greater, which
//    IS plain `>`.
//
// The 20.0f divisor is 0x41A00000 lifted from 0x180161700, not a decimal
// retyped (reference_crt_coefficients_must_be_lifted_not_retyped.md); it is
// exactly representable, so the two spellings agree here, and the bits are
// recorded so a future edit cannot quietly change that.
//
// ORACLE: FULL. No stores, no allocation, no callee — call ours, call theirs on
// the same unmodified inputs, compare the returned int. `calls=` therefore
// counts real work: a zero means no positional sound was played, not agreement.
// ============================================================================
int32_t Scene::FindFree3DEffect(Object::ExternalPtr<SoundEffect> effect,
                               const System::Time& time, const Vector3& pos) const {

    // The reuse window, in ticks. Retail seeds it from Scene::time — which is a
    // tick count — and only replaces it when the effect declares a longer one.
    int64_t window = this->time;
    // FAITHFUL UNGUARDED DEREF: retail's first instruction reads RDX+0x28.
    const SoundEffect* const fx = effect.Get();
    const int64_t declared = fx->_maxLength;                  // MOV RAX,[RDX+0x28]
    if (declared != 0) {
        const float kTwenty = 20.0f;                 // 0x41A00000 @ 0x180161700
        const float seconds = static_cast<float>(declared) / kTwenty;
        // NOT RETAIL (review F-136, undefined behaviour): CvttSd2Si64 is the
        // CVTTSD2SI below without the C++ cast's UB for a huge _maxLength; it
        // gives the instruction's INT64_MIN, as retail does.
        if (seconds > static_cast<float>(window))    // COMISS/JBE: ordered, strictly greater
            window = Audio::CvttSd2Si64(static_cast<double>(seconds) /
                                        RtSystem::TimerSecondsPerTick());
    }

    const float radius = fx->_radius;                         // MOVSS XMM3,[RDX+0x20]
    const float radiusSq = radius * radius;

    const EffectSlot* slot = effects._data;
    int32_t firstFree = -1;
    int32_t ours = -1;
    bool early = false;
    for (int32_t i = 0; i < kEffectSlotCount; ++i, ++slot) {
        if (firstFree < 0 && slot->idTag == 0)
            firstFree = i;
        if (slot->soundEffectRef == effect) {
            const float dx = pos.x - slot->pos.x;
            const float dy = pos.y - slot->pos.y;
            const float dz = pos.z - slot->pos.z;
            const float d2 = (dy * dy + dx * dx) + dz * dz;   // retail's association
            if (!(d2 >= radiusSq)) {                          // COMISS/JNC: CF==1, NaN included
                int64_t dt = slot->startTime._ticks - time._ticks;
                if (dt < 0) dt = -dt;
                if (dt < window) { ours = -1; early = true; break; }
            }
        }
    }
    if (!early)
        ours = firstFree;

    // EFFECTIVENESS CONTROL. `calls=` on the port says only that the body ran.
    // This function has three outcomes and they exercise different code: the
    // early -1 is the ONLY path that reaches the distance test and the reuse
    // window (i.e. the only one that touches SoundEffect+0x20/+0x28 for real),
    // and "no free slot" is the 4096-iteration boundary. A green summary made
    // entirely of the first-free case would leave the arithmetic this port was
    // written for completely unverified -- the SetValueInt shape.

    return ours;
}

// ============================================================================
// uint Audio::Scene::PlayEffect3D(ExternalPtr<SoundEffect> soundRef,
//     Scene::SoundType type, const Time& time, const Vector3& pos)
// retail 0x1800deeb0. Delegates slot SELECTION to FindFree3DEffect (retail
// 0x1800deae0/ord 2352).
//
// WAVE 9 LANE W, 2026-09-08 — this comment used to end here saying that helper
// "dereferences Audio::SoundEffect internals (a class this lane does not own
// — none of SoundBuffer/SoundEffect/Stream is in this wave, WAVE8.md §2)", and
// that calling into retail for that ONE sub-decision was the same "call the DLL
// for what you don't own" pattern used elsewhere. That was true while the DLL
// existed and is FALSE at cutover, where the call is through null. It is now
// PORTED, immediately above, and the blocker did not survive reading the body:
// the SoundEffect "internals" are two scalars at +0x20 and +0x28, and the
// function makes no store of any kind. RT_EXPORT_ONCE below therefore now
// reaches our own body rather than falling through to a null.
// SHADOW oracle (mutates the slot; verified via read-only FindEffect3D after).
// ============================================================================
uint32_t Scene::PlayEffect3D(Object::ExternalPtr<SoundEffect> effect, SoundType soundType,
                             const System::Time& time, const Vector3& pos) {
    uint32_t handle = 0;
    if (effect) {
        // B.3.7 (B7-11): was RT_EXPORT_ONCE by name; the member is this class's.
        int32_t idx = FindFree3DEffect(effect, time, pos);
        if (idx >= 0) {
            EffectSlot& slot = effects._data[idx];
            slot.soundEffectRef = effect;
            slot.soundType = (uint32_t)soundType;
            slot.pos.x = pos.x; slot.pos.y = pos.y; slot.pos.z = pos.z;
            slot.startTime = time;
            slot.channelHandle = 0;
            playGeneration += 1;
            handle = ((uint32_t)idx << kHandleIndexShift) | kEffect3DBit
                     | ((uint32_t)playGeneration << kEffectGenerationShift);
            slot.idTag = handle;
        }
    }

    return handle;
}

// ============================================================================
// void Audio::Configuration::Save() — retail 0x1800dde10. Four
// IO::Persistence::Set(int) calls, already ported (Wave 4 lane C,
// src/engine/io/persistence.cpp) but called here via the raw DLL door
// against the shipped DLL, matching this project's convention that a lane
// stands alone against the DLL rather than depending on another lane's port
// being enabled. SHADOW oracle: verified via IO::Persistence::Get(int) — a
// full round trip, same shape as Wave 4's own Set(int) oracle.
// ============================================================================
void Configuration::Save() {

    // ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, read fresh.
    ::IO::Persistence* persistence = ::IO::Persistence::_singleton;

    // IO::Persistence::Set(int) (0x1800b80c0), called by name (B3R).
    if (persistence) {
        persistence->Set("Audio", "UIVolume",      static_cast<int>(uiVolume));
        persistence->Set("Audio", "MusicVolume",   static_cast<int>(musicVolume));
        persistence->Set("Audio", "AmbientVolume", static_cast<int>(ambientVolume));
        persistence->Set("Audio", "EffectVolume",  static_cast<int>(effectVolume));
    }

}

// ============================================================================
// bool Audio::Configuration::Startup() — retail 0x1800ddee0. Retail does NOT
// call the exported Persistence::Get(int) here — it inlines its own
// "%s%s"-formatted key lookup straight against Persistence's map (Map_Find,
// retail 0x18002ace0) and a bare strtol. Transcribed exactly rather than
// routed through Get(int), since that is what retail's own bytes do; the two
// are functionally identical (same key format, same map, same strtol), which
// is exactly what the diff below checks.
// ============================================================================
bool Configuration::Startup() {

    // ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, read fresh.
    // IO::Persistence::Get(int&) (0x1800b81e0), called by name (B3R).
    const ::IO::Persistence* persistence = ::IO::Persistence::_singleton;

    // Retail leaves the field untouched (whatever the ctor/last value set) when
    // the key is not found — Get(int)'s own contract, which this reproduces by
    // only writing on success.
    if (persistence) {
        int v;
        if (persistence->Get("Audio", "UIVolume", v))      uiVolume = v;
        if (persistence->Get("Audio", "MusicVolume", v))   musicVolume = v;
        if (persistence->Get("Audio", "AmbientVolume", v)) ambientVolume = v;
        if (persistence->Get("Audio", "EffectVolume", v))  effectVolume = v;
    }

  // retail's own body always returns true (no failure path)
    return true;
}

// ============================================================================
// Registration.
// ============================================================================
// ============================================================================
// Audio::Scene::Serialize(IO::Serializer&)   retail 0x1800e0050  ord 3730
// ============================================================================
// UNBLOCKED AND PORTED 2026-09-07 (Wave 9 lane A). Wave 8 lane C held this back
// because THAT LANE "did not independently re-derive retail's own IO::Encoding
// vtable offsets within its budget", and a wrong guess desyncs every save
// silently. It did not have to: Wave 4 had already written retail's offsets
// into the warning block at the top of src/engine/io/rt_io_port.cpp -- bool
// +0x78 / uint +0x98 / int +0xa0, against the profile build's +0x90/+0xb0/+0xb8
// -- and src/engine/io/Serializer.h's `rw_vtbl` maps all 26 slots.
//
// Disassembling 0x1800e0050 narrows the exposure to ONE slot: the function
// touches exactly one Encoding entry, kScalarInt (+0xa0), plus kArrayBool
// (+0x20) on the ReaderWriter. 40 instructions.
//
// SHAPE (0x1800e0050), kept:
//   * a LOCAL Serializer is built on the stack holding the caller's Stream and
//     a null ReaderWriter, and it is that local -- not the caller's Serializer
//     -- that is handed to the bulk element pass. Retail never writes through
//     the caller's object.
//   * the ReaderWriter comes from the Encoding factory pair, +0x20 when the
//     stream is WRITING and +0x18 when reading (`CMP [stream+8],0; JNZ`).
//   * the effect count is serialized as a scalar int, and ONLY on the reading
//     side is the array resized to it (0x1800e00c0's JNZ skips the resize when
//     direction != 0) -- so a save never reallocates what it is saving.
//   * `paused[5]` goes out as kArrayBool with n == 5 (`LEA EDX,[R9+0x5]` after
//     `XOR R9D,R9D`, R8 = this+0x30).
//   * the ReaderWriter is released through its scalar-deleting dtor with
//     flags == 1.
//
// The two unexported callees are ordinary Collection internals, called by
// address exactly as Input::KeyMap::ClearAll and RegisterInput already do:
//   0x180051390  Collection::Array<EffectSlot>::Resize(int)   0x40-byte
//                elements, 3x growth heuristic, allocator-locked free
//   0x180052640  the bulk element serialize
// Neither is reimplemented here; both are shared Collection code whose own port
// belongs to whoever ports Collection, not to Audio.
//
// *** CORRECTED, BATCH 8b: 0x180052640 IS NOT Collection CODE. *** It is an
// Audio body -- it walks Audio::Scene::EffectSlot and calls the SoundEffect
// ExternalPtr serialize -- and it is now ported and registered in
// src/engine/audio/audio_effectslot_serialize.cpp together with its inner-array
// callee 0x180051e00. Only the resize it reaches on the read path
// (0x180052330) is Collection code, and that one is in
// src/engine/collection/array.cpp as kSpec_EmitterInstance. The
// `ResolveAddr(Module::Runtime, 0x180052640)` below therefore lands on our body
// now, because the resolver consults the port registry first; the call site
// needs no change.
// WAVE 9 LANE Q did that for the first of the two: 0x180051390 is now a
// registered port (kSpec_EffectSlot in src/engine/collection/array.cpp) and
// still calls retail's per-element copy-assign 0x180051650 and destroy-range
// 0x180052110 by address. 0x180052640 WAS untouched and still the DLL's; as of
// BATCH 8b it is ours (audio_effectslot_serialize.cpp).
// One fact from that reading matters to this file: the EffectSlot default
// element leaves bytes +0x2c..+0x2f UNWRITTEN, so a byte comparison of a
// freshly grown _effects array must skip that window per element.
//
// ORACLE: the Wave 9 lane S save-stream A/B harness
// (the Wave 9 lane S emission oracle). There is no call-level replay for anything on
// the save path -- ours then theirs would emit twice into one stream -- but on
// the WRITE path a Serialize is a PURE READ of the object, so the two calls can
// be replayed into two INDEPENDENT recording sinks and their emission sequences
// compared. That is what the emission oracle does, and it is why this port
// is not held back for "no oracle".
// ENDGAME B.3.2 step 2 — the 0x180051390 by-address forwarder (`arrayResize`)
// is GONE: `scene->effects` is already `Collection::Array<EffectSlot>`
// (src/engine/audio/Audio.h), and 0x180051390 IS that instantiation's
// SetSize — `kSpec_EffectSlot` (src/engine/collection/array.cpp), NON-TRIVIAL
// (a real per-element copy-assign and destroy-range, not a byte trait), so it
// stays a hand-derived `rtcoll::AsArray<&kSpec_EffectSlot>` bridge rather than
// a plain `.SetSize(n)`. The row stays registered in array.cpp.
void Scene::Serialize(IO::Serializer& serializer) {
    IO::Serializer* ser = &serializer;

    // Retail's stack-local Serializer: the caller's Stream, a null ReaderWriter
    // until the Encoding's factory (+0x20 writing, +0x18 reading, version 0)
    // fills it, and `if (rw) delete rw` at the end -- IO::Serializer's ctor
    // and dtor, inlined. NOT the caller's object.
    IO::Serializer local(*ser->_stream, 0);
    IO::ReaderWriter* rwc = local._readerWriter;

    // SerializeInt (+0xa0) -- (int& value, const char* name)
    int32_t count = effects.GetSize();
    rwc->SerializeInt(count, nullptr);

    // Reading only: grow the array to the count just read. Retail's JNZ at
    // 0x1800e00c0 skips this when writing, so saving never reallocates.
    // retail 0x180051390 — kSpec_EffectSlot; see the banner above.
    if (local._stream->_direction == IO::kReading)
        rtcoll::AsArray<&rtcoll::kSpec_EffectSlot>(&effects)->SetSize(count);

    // 0x180052640 — this directory's own body, called directly (B.3.7:
    // the function-local first-call cache that resolved it by address is gone).
    RtAudio_Scene_SerializeEffectSlots(&local, count, effects.GetData());

    // SerializeBoolArray (+0x20) -- (int n, bool* data, const char* name), n == 5.
    rwc->SerializeBoolArray(5, paused, nullptr);
}   // ~Serializer: `if (rw) delete rw`

// ============================================================================
// Wave 8.5 (close-out) — the Audio channel-teardown family
// ============================================================================
// WAVE8_5.md §9 concluded that the six Audio holdbacks "share ONE root cause,
// a stored completion callback fired through a function pointer", and that the
// lane therefore "needs a Device-level fake, not a better diff".
//
// The root cause is right. **The conclusion does not follow, and reading the
// five retail bodies per function instead of as a family shows why:**
//
//     the Device is reachable ONLY through a non-null
//     `Channel::platformChannel[b]` — and that is a field of the SCENE.
//
// Every device call in this family — `Device::Stop` (0x1800de200), and the
// sequence `StopAll` inlines instead of calling it — sits inside
// `if (platformChannel[b] != 0)`. Nothing here dereferences the Device
// singleton before that test; `StopAll` loads it into RBX at 0x1800df192 and
// `StopEffect`/`PauseEffect` into RBP/R14 on entry, but the first instruction
// that touches what it points at is behind the null check.
//
// So a SYNTHETIC Scene whose 256 channels have null `platformChannel[]` puts
// the device out of reach BY CONSTRUCTION, and both implementations can then
// be run side by side over the whole state space — no interposer, no recorder,
// no fake. That is what `ProbeAudioScene` in standalone/rt_orphan_probe.cpp
// does, and it is why these three CALLED holdbacks land here rather than
// waiting on a tool. The remaining three (`Scene::Update`, `Device::Update`,
// `Device::FreeChannelPlatform`) were NOT unblocked by it: the first consumes
// System::Random state and the other two are device internals, where the
// device is the subject rather than an unreachable callee. (All three have
// since been ported: audio_scene_update.cpp and audio_device.cpp, members of the
// real classes since ENDGAME B.3.7.)
//
// This is WAVE8_5.md §8's own correction happening a fifth time: a group-level
// claim over these functions failed per-function checking. Recorded, not fixed
// silently.
//
// WHAT THE LIVE PATH GETS. On a real scene the platform channels are NOT null,
// so there is no call-level replay: running both bodies would fire every live
// effect's completion callback twice (§9's point, which stands). Each function
// below therefore carries a SHADOW check instead — after our body runs, the
// DLL's own read-only observer is asked whether the resulting state is what it
// should be — exactly the pattern `PlayEffect` above already uses. The full
// state comparison is the probe's job; the shadow's job is to give the live
// path a `calls=` number and catch a port that stopped agreeing with reality.
//
// RETAIL READS UNINITIALISED STACK, and a byte oracle must know it.
// `StopEffect` (0x1800df5e6) and `StopAll` (0x1800df2af) both build the reset
// Channel by writing a DWORD to a stack slot and reading a QWORD back out:
//     MOV dword ptr [RSP+0x28],EDX      ; 4 bytes written
//     MOV RAX,qword ptr [RSP+0x28]      ; 8 bytes read
//     MOV qword ptr [RSI+RCX+0x8],RAX   ; soundType=0, _pad0c=STACK GARBAGE
// and the same shape at [RSP+0x88] for +0x68 (handleTag=0, started=0,
// _pad6d[3]=garbage). Both landing fields are the padding this file's type
// header already documents as never read. We write zeroes; the probe EXCLUDES
// those 7 bytes per channel rather than pretending to reproduce a caller's
// stack (reference_oracle_must_not_compare_unwritten_bytes.md, in reverse —
// there the trap was comparing bytes NEITHER side wrote, here it is bytes only
// one side can write meaningfully).
// ============================================================================

// Audio::Device::Stop(AudioChannel*) — retail 0x1800de200, ord 4105. The one
// device entry point this family needs, and the one that fires the stored
// completion callback (WAVE8_5.md §9). A direct member call since B.3.7.

// Release every live platform channel hanging off one Channel record.
// `clearSlots` reproduces the one behavioural difference between the two
// retail callers: PauseEffect nulls the pointer and clears bit 1 of the flag
// byte as it goes (0x1800df50d/0x1800df515), because the Channel record
// SURVIVES a pause; StopEffect does not bother (0x1800df5a3..0x1800df5bf),
// because the whole record is overwritten with the default two instructions
// later.
static void ReleasePlatformChannels(Scene* self, uint32_t index, bool clearSlots) {
    // ?_singleton@Device@Audio@@ (0x1801fe760): retail's `MOV RBX,[..]`.
    Audio::Device* device = Audio::Device::_singleton;
    for (int b = 0; b < 8; ++b) {
        // Retail RE-READS channels._data on every iteration (the callee could
        // in principle move it); reproduced rather than hoisted.
        Channel* ch = &self->channels._data[index];
        Audio::Device::AudioChannel* p = ch->platformChannel[b];
        if (!p) continue;
        // B.3.7 (B7-11): was RT_EXPORT_ONCE `?Stop@Device@Audio@@`; now the member.
        device->Stop(p);
        if (clearSlots) {
            ch = &self->channels._data[index];
            ch->platformChannel[b] = nullptr;
            ch->channelFlags[b] &= Bits(~SampleFlags::Ended);
        }
    }
}

// The default-constructed Channel every teardown path writes back, per
// FUN_180051020 and reconfirmed by StopEffect's and StopAll's identical reset
// blocks: everything zero except volume and sampleVolume, which are 1.0f.
static void ResetChannel(Channel* ch) {
    ch->soundEffectRef = nullptr;
    ch->soundType      = 0;
    ch->_pad0c         = 0;   // retail leaves stack garbage here; see the header
    ch->startTime._ticks = 0;
    ch->volume         = 1.0f;
    ch->sampleVolume   = 1.0f;
    for (int b = 0; b < 8; ++b) ch->platformChannel[b] = nullptr;
    for (int b = 0; b < 8; ++b) ch->channelFlags[b] = 0;
    ch->handleTag = 0;
    ch->started   = 0;
    ch->_pad6d[0] = ch->_pad6d[1] = ch->_pad6d[2] = 0;   // ditto
}

// The channel-handle validity test, spelled once. `(stored ^ handle) & top-23`
// — the encoding this file's type header documents. Retail repeats it inline
// in PauseEffect, UnpauseEffect, StopEffect, StopAll, Pause and
// IsEffectPlaying.
static bool ChannelHandleMatches(const Scene* self, uint32_t handle, uint32_t* outIndex) {
    uint32_t idx = (handle >> Scene::kHandleIndexShift) & Scene::kChannelIndexMask;
    *outIndex = idx;
    return ((self->channels._data[idx].handleTag ^ handle) & Scene::kChannelTagMask) == 0;
}

// ============================================================================
// void Audio::Scene::PauseEffect(uint handle) — retail 0x1800df4a0, ord 3314
// ============================================================================
// Releases the platform channels of one live effect but KEEPS the Channel
// record, so the effect can be restarted by clearing `started` later — which
// is exactly what Pause's unpause direction does. Not called from our tree
// directly; it is here because Scene::Pause CALLs it (retail does not inline
// it) and a port of Pause that left this on the DLL would be the half-ported
// hazard WAVE8.md §1 warns about.
// ============================================================================
void Scene::PauseEffect(uint32_t handle) {
    uint32_t idx;
    const bool matched = ChannelHandleMatches(this, handle, &idx);
    if (!matched) return;
    ReleasePlatformChannels(this, idx, /*clearSlots=*/true);
}

// ============================================================================
// void Audio::Scene::StopEffect(uint handle) — retail 0x1800df550, ord 4109
// ============================================================================
// Releases the platform channels AND resets the Channel record to default, so
// the handle stops validating. Reached from our tree only through
// StopEffect3D; ported for the same reason as PauseEffect.
// ============================================================================
void Scene::StopEffect(uint32_t handle) {
    uint32_t idx;
    const bool matched = ChannelHandleMatches(this, handle, &idx);

    if (!matched) return;
    ReleasePlatformChannels(this, idx, /*clearSlots=*/false);
    ResetChannel(&channels._data[idx]);

    // POST-CONDITION. Stated as "the record was zeroed", NOT as "the handle no
    // longer validates" -- the first attempt said the latter and it is FALSE
    // for a handle whose generation bits are all zero, which validates against
    // a zeroed tag in retail too. That predicate reported 2 divergences in 236
    // live calls and every one of them was the PREDICATE being wrong about
    // retail, not the port (reference_shadow_predicate_must_name_its_failing_term.md).
}

// ============================================================================
// void Audio::Scene::Pause(int soundType, bool paused) — retail 0x1800df070
// ============================================================================
// Called six times a menu->game cycle (mainmenudialog.cpp pausing types 4/3/2
// on open, gameupdateloop_load.cpp unpausing them after a load).
//
// The two directions are not symmetric, and the asymmetry is the function:
//   paused = true   -> PauseEffect every live channel of this sound type,
//                      which releases its platform channels.
//   paused = false  -> for every live channel of this type, re-validate the
//                      handle it stores AGAINST ITSELF and clear `started`,
//                      so Scene::Update restarts it. This direction calls
//                      NOTHING — it is `UnpauseEffect` (0x1800df470) inlined,
//                      instruction for instruction.
// Retail strides a HARDCODED 0x7000 bytes (256 channels), not `_size`;
// reproduced.
//
// The leading early-out (`if (paused[soundType] == paused) return;`) is the
// reason a naive ours-then-theirs replay would report green having compared
// nothing — WAVE8_5.md §9's observation, and the `RegisterInput` trap. The
// probe steps INDEPENDENT scenes, so it does not apply there.
//
// `soundType` is sign-extended (MOVSXD RSI,EDX) and used unchecked as an index
// into `paused[5]`; a negative or >4 value reads and writes out of bounds in
// retail too. Left faithful (CLAUDE.md: do not "fix" a faithful deref).
// ============================================================================
void Scene::Pause(int32_t soundType, bool paused) {
    int64_t t = soundType;
    const bool alreadyThere = (this->paused[t] == paused);

    if (!alreadyThere) {
        if (paused) {
            for (int32_t i = 0; i < kChannelCount; ++i) {      // 0x7000 bytes, hardcoded
                const Channel* ch = &channels._data[i];
                uint32_t tag = ch->handleTag;
                if (tag == 0 || (int32_t)ch->soundType != soundType) continue;
                PauseEffect(tag);
            }
        } else {
            for (int32_t i = 0; i < kChannelCount; ++i) {      // 0x7000 bytes, hardcoded
                const Channel* ch = &channels._data[i];
                uint32_t tag = ch->handleTag;
                if (tag == 0 || (int32_t)ch->soundType != soundType) continue;
                uint32_t idx;
                if (!ChannelHandleMatches(this, tag, &idx)) continue;
                channels._data[idx].started = 0;
            }
        }
        this->paused[t] = paused;
    }
}

// ============================================================================
// void Audio::Scene::StopEffect3D(uint id) — retail 0x1800df370
// ============================================================================
// Stops the Channel a 3D effect is playing on (if any) and resets its
// EffectSlot to default. The slot's `positions` array is released through
// Collection's own array-assign-from-empty, called by address exactly as
// Scene::Serialize calls its two Collection internals.
//
// The guard is spelled differently from FindEffectSlot's and means the SAME
// THING. Retail compares the generation halves (`id >> 13` against
// `idTag >> 13`, 0x1800df3a7/0x1800df3aa); FindEffectSlot masks with
// 0xffffe000, which is bits 13..31 -- the generation, and nothing else. An
// earlier draft of this comment claimed they "differ by one bit" and built a
// shadow predicate on that claim; the predicate reported 56 divergences in 96
// live calls, all of them the claim being wrong. Retail's spelling is kept
// here because it is retail's, not because it differs.
// ============================================================================
// 0x180050d30 is `Collection::Array<EmitterInstance>::operator=`, retail's
// DEEP assign (Collection.h's `Assign`; stride 0x10, align 4, the four-DWORD
// element copy: introsort.cpp's PART 2 banner). B9 RT (D4): it was a hand port
// (`rtsort::Array16Assign`) behind a forwarder here.
//
// 0x180051d80 (the dtor) IS converted below: it is a J1 body (array.cpp's
// J1_ENTRY table), i.e. trivial-destroy `Array<uint8_t>::~Array`, and
// `EffectSlot::positions` is `Collection::Array<EmitterInstance>`
// (src/engine/audio/Audio.h) with `EmitterInstance` a trivially destructible
// POD, so the trait-derived `~Array()` on the stack temporary IS that body.

void Scene::StopEffect3D(uint32_t id) {
    uint32_t idx = (id >> kHandleIndexShift) & kEffectIndexMask;
    EffectSlot* slot = &effects._data[idx];
    if ((slot->idTag >> kEffectGenerationShift) != (id >> kEffectGenerationShift)) {
        // SHADOW on the reject path -- reported rather than returned silently,
        // for the same reason as Pause's early-out: `calls=` must mean
        // "entered". The DLL's GetEmitterCount uses a guard one bit WIDER than
        // this one (0xffffe000 against the generation-only compare here), so a
        // reject here does NOT imply a reject there; what it does imply is that
        // the slot is untouched, which is what is asserted.
        // The DLL's GetEmitterCount validates with `(idTag ^ id) & 0xffffe000`,
        // which is bits 13..31 -- the generation, and NOTHING ELSE. That is the
        // same predicate this guard spells as `(idTag >> 13) != (id >> 13)`, so
        // a reject here means a reject there and GetEmitterCount must answer 0.
        // A full replay of the guard, against the DLL, on live ids.
        return;
    }

    if (slot->channelHandle != 0)
        StopEffect(slot->channelHandle);

    // Retail's stack-local default EffectSlot; only its `positions` member is
    // used (as the empty source of the assign), but the whole record is built.
    EffectSlot tmp{};
    tmp.baseVolume = 1.0f;

    slot->pos.x = slot->pos.y = slot->pos.z = 0.0f;
    slot->soundType      = 0;
    slot->soundEffectRef = nullptr;
    slot->startTime._ticks = 0;
    slot->channelHandle  = 0;
    slot->idTag          = 0;
    slot->baseVolume     = 1.0f;
    slot->positions = tmp.positions;                // 0x1800df441: CALL 0x180050d30
    // retail 0x180051d80 (`tmp`'s ~Array() tail call) is GONE, not converted to
    // an explicit `.~Array()` call: `tmp` is a local EffectSlot, and now that
    // `Collection::Array<EmitterInstance>` carries a real destructor,
    // EffectSlot's IMPLICIT destructor already calls `tmp.positions.~Array()`
    // when `tmp` goes out of scope at the end of this function — calling it
    // here too would be a double ~Array() on the same buffer (a double free,
    // harmless only because `tmp.positions` is always empty, but still wrong).
    // One destruction per path, per the locals rule.

    // SHADOW: the DLL's read-only GetEmitterCount on the id we just retired
    // must report 0 -- the slot is default and its idTag no longer validates.
}

// ============================================================================
// void Audio::Scene::StopAll() — retail 0x1800df150, ord 4107
// ============================================================================
// Called once, from GameUpdateLoop teardown (gameupdateloop.cpp:2880).
//
// It is `StopEffect` applied to all 256 channels, with `Device::Stop`'s body
// INLINED rather than called — and the inlining is exact: 0x1800df1eb's
// `CMP [device+0xa88],0` guard is `Device::StopPlatform` (0x1800dea20)'s own
// first instruction, 0x1800df25b calls `FreeChannelPlatform` (0x1800de6f0)
// just as Device::Stop does, and 0x1800df260..0x1800df289 is Device::Stop's
// trailer instruction for instruction (clear the low 2 flag bits, null the
// callback slot, then fire the callback that was in it). So delegating to the
// exported `Device::Stop` is not an approximation of this body — it is the
// same code with the call re-introduced.
//
// The INDIRECTION is faithful and deliberate: the outer loop reads the tag
// from channel `i` but does all its work on channel `(tag>>1)&0xff`. Those are
// the same channel for any handle the Scene itself minted, so the difference
// is invisible in a normal run — and reproducing it costs one line.
// ============================================================================
void Scene::StopAll() {

    // The set this call is OBLIGED to retire, computed BEFORE any of it runs
    // and computed with the DLL's OWN predicate (IsEffectPlaying is
    // `((storedTag ^ handle) & 0xfffffe00) == 0` on the channel the handle
    // names -- exactly the test at 0x1800df1ac). Deciding the obligation after
    // the fact is what made the first version of this shadow wrong: "no channel
    // still has a tag" is FALSE of retail, because a channel whose tag names a
    // DIFFERENT channel keeps its own tag while the other one is reset. That
    // predicate reported 8 divergences in 9 calls and all 8 were the predicate.

    for (int32_t i = 0; i < kChannelCount; ++i) {              // 0x7000 bytes, hardcoded
        uint32_t tag = channels._data[i].handleTag;
        if (tag == 0) continue;
        uint32_t idx;
        if (!ChannelHandleMatches(this, tag, &idx)) continue;
        ReleasePlatformChannels(this, idx, /*clearSlots=*/false);
        ResetChannel(&channels._data[idx]);
    }

}

}  // namespace Audio

// ---------------------------------------------------------------------------
// Audio::Configuration::~Configuration() - retail 0x1800DE0D0, ord 554.
// WAVE 23 LANE TAIL.
//
// The WHOLE retail body, read at its real address in the shipped DLL:
//
//   1800de0d0  MOV qword ptr [0x1801fe758],0x0
//   1800de0db  RET
//
// Two instructions. It destroys nothing - it clears the class's own singleton
// pointer and returns. No base dtor, no member dtor, no free (the caller frees:
// src/engine/engine/engine_lifecycle.cpp does `dtor(p); MemFree(p);`, which is
// what the exported `??1` spelling means).
//
// `QEAA` is the NON-VIRTUAL dtor spelling and it is the one the export table
// carries; the virtual spelling `??1Configuration@Audio@@UEAA@XZ` does not
// exist in this DLL and would resolve NULL silently
// (reference_phantom_dtor_mangling_qeaa_vs_ueaa.md). Checked with pefile
// against the export directory before this body was written, not assumed.
//
// THE ADDRESS IT WRITES IS `?_singleton@Configuration@Audio@@0PEAV12@EA`
// (0x1801FE758, ord 4801) - one of this lane's own DATA rows. That identity is
// not a guess: the export directory maps ord 4801 to exactly 0x1801FE758.
//
// SO THE STORE GOES THROUGH THE DOOR, not through a hard-coded address. With
// the DATA row unpromoted the door IS the DLL's slot and this is byte-identical
// to retail; if the row is ever promoted, this writer moves with it instead of
// being stranded writing a slot nobody reads. That is the whole point of
// porting a two-instruction function: it converts retail's un-interceptable
// RIP-relative store into a door. See this lane's report for why the row is
// NOT promoted today regardless.
// ---------------------------------------------------------------------------
// `this` is genuinely unused - retail's two instructions never touch RCX.
Audio::Configuration::~Configuration() {
    _singleton = nullptr;
}

// ?_singleton@Configuration@Audio@@0PEAV12@EA (0x1801fe758) -- the class static (ENDGAME B.3.9 ENG, B39_RULES D7);
// RtAudioPortInit redirects the minted storage row onto it.
::Audio::Configuration* ::Audio::Configuration::_singleton = nullptr;
