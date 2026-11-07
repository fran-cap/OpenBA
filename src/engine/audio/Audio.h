#pragma once
// ============================================================================
// src/engine/audio/Audio.h — Audio::Scene / Audio::Configuration (real classes)
// ============================================================================
//
// Wave 8, lane C of the Runtime-x64.dll port (docs/runtime_port/PLAN.md §4.2,
// docs/history/runtime_port/WAVE8.md, docs/history/runtime_port/WAVE8_LANE_C.md). Every offset
// below was read out of a decompiled+disassembled Runtime-x64.dll function
// (image base 0x180000000, read 2026-09-07); the function and field access
// that pins it is named in the comment beside it.
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1.
//
// WHAT Audio::Scene IS
// ---------------------
// One live audio scene: a fixed-size table of up to 256 concurrently playing
// "Channels" (2D/general sound-effect handles allocated by PlayEffect) and a
// SEPARATE fixed-size table of up to 4096 "EffectSlots" that is a single,
// id-keyed store shared by TWO different callers:
//   - Audio::Scene::AddEmitter/MoveEmitter/RemoveEmitter/SetEmitterVolume/
//     GetEmitterCount, which use the caller-supplied `id` directly as a
//     bucket key (ambient/building emitters — one slot per emitter GROUP,
//     with a NESTED array of individual instance positions+volumes hanging
//     off the slot: `EffectSlot::positions`); and
//   - Audio::Scene::PlayEffect3D/FindEffect3D/StopEffect3D, which allocate a
//     fresh id from `Scene::_playGeneration` and use ONE EffectSlot per
//     playing 3D sound effect (`EffectSlot::soundEffectRef`, `::pos`).
// Nothing distinguishes the two USES of an EffectSlot at the type level —
// retail really does share the table — so this header carries every field
// either caller writes and leaves it to the .cpp to know which half it uses.
//
// ID / HANDLE ENCODING (two independent 32-bit handle spaces; do not confuse
// them — they index DIFFERENT tables with DIFFERENT bit layouts):
//   Channel handle   (PlayEffect,     IsEffectPlaying):
//       bits 0     = 0 (never set on this side)
//       bits 1..8  = channel index (0..255), i.e. `index << 1`
//       bits 9..31 = Scene::_playGeneration at allocation time
//     Validity check used throughout: `(storedTag ^ handle) & 0xfffffe00 == 0`
//     (top 23 bits — index bit 8 plus the whole generation — must match).
//   EffectSlot id    (AddEmitter's caller-supplied id, PlayEffect3D,
//                      FindEffect3D/StopEffect3D/GetEmitterCount):
//       bits 0      = 1 for a PlayEffect3D-allocated slot, 0 for an
//                      AddEmitter-owned emitter-group id (never checked by
//                      this bit alone; AddEmitter's caller picks its own ids)
//       bits 1..12  = slot index (0..4095), i.e. `index << 1`
//       bits 13..31 = Scene::_playGeneration at allocation time
//     Validity check: `(storedTag ^ id) & 0xffffe000 == 0` (top 19 bits).
//
// EVIDENCE (retail Runtime-x64.dll addresses):
//   ??0Scene@Audio@@QEAA@XZ                 0x1800e0160  ctor: zeroes fields,
//       grows _effects to 0x1000 (FUN_180051390), _channels to 0x100
//       (FUN_180051020). NEITHER OF THOSE HELPERS' FIRST WRITE IS A VTABLE
//       POINTER — `*(undefined8*)this = 0` is the very first store and it is
//       Scene::_time, not a vtable slot. Consistent with no `??_7Scene@Audio@@`
//       export.
//   ??1Scene@Audio@@QEAA@XZ                 0x1800e0120  dtor.
//   FUN_180052110 (nested-array teardown, called from ~Scene on _effects
//       before freeing the outer buffer) frees `EffectSlot::positions._data`
//       for every live slot — the ONE owned pointer inside EffectSlot.
//   ?AddEmitter@Scene@Audio@@…               0x1800dedf0
//   ?MoveEmitter@Scene@Audio@@…               0x1800dec40
//   ?RemoveEmitter@Scene@Audio@@…             0x1800decf0
//   ?SetEmitterVolume@Scene@Audio@@…          0x1800ded60
//   ?GetEmitterCount@Scene@Audio@@…           0x1800dec00
//   ?FindEffect3D@Scene@Audio@@…              0x1800dee70
//   ?PlayEffect@Scene@Audio@@…                0x1800def60  (the REAL, QEAA-
//       spelled export; see rt_audio_port.cpp's phantom-name note)
//   ?PlayEffect3D@Scene@Audio@@…               0x1800deeb0
//   ?IsEffectPlaying@Scene@Audio@@…            0x1800df680
//   FUN_180051aa0 = Collection::Array<Vector4>::SetSize(n, fillValue) — the
//       SAME growth policy documented in Collection.h's Array<T> comment,
//       independently confirmed a third time here (two more instantiations
//       already cross-checked there).
//   ?Save@Configuration@Audio@@…               0x1800dde10
//   ?Startup@Configuration@Audio@@…            0x1800ddee0
//
// ENDGAME B.3 step 7, block B4: the `RtAudio::` PODs this header used to carry
// ARE the classes now (one definition, B7-6): `Audio::Scene` with its three
// nested value types and `Audio::Configuration`, both NON-polymorphic. The
// `RtAudio::` names survive as aliases for the unconverted includers
// (src/engine/engine/singleton_ctors.cpp asserts a Configuration offset).
// The other four Audio classes have their own headers beside this one:
// SoundBuffer.h, SoundEffect.h, Stream.h, Device.h. THREE classes are spelled
// `Scene` (`Audio::Scene`, `Picking::Scene`, the Graphics one): always qualify.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>
#include "../collection/Collection.h"
#include "../global_types.h"   // Vector3
#include "../object/External.h"             // Object::ExternalPtr
#include "Device.h"                         // Device::AudioChannel (OnSoundEnded)
#include "../system/Value.h"                // System::Time (the two startTime fields)
#include "../system/EnumFlags.h"            // OPENBA_ENUM_FLAGS (SampleFlags)

namespace System { class Time; class Random; }
namespace IO { class Serializer; }

namespace Audio {

class SoundEffect;

// Scene::Channel::channelFlags[i]: one byte per platformChannel slot.
enum class SampleFlags : uint8_t {
    Started = 0x01,   // the sample was started (or skipped as started) this playback
    Ended   = 0x02,   // its Device channel ended (OnSoundEnded ORs the byte)
};
OPENBA_ENUM_FLAGS(SampleFlags)

// ============================================================================
// Audio::Scene — 0x38 (56) bytes, NON-polymorphic, concrete.
//
// NO VTABLE: the ctor's first store is `*(undefined8*)this = 0` (Scene::time),
// and there is no `??_7Scene@Audio@@` export.
//
// HELD BY VALUE (B37_RULES 2.4 and its addendum): the exe's MenuDialogLoopT
// embeds one at +0xb30 and GameUpdateLoop::Load allocates another
// (`MemAlloc(0x38,8)` at 0x14009706a); both holders are unconverted exe PODs
// that construct in place through `RtAudio_Scene_Ctor`, which runs the C++
// default constructor `Scene()` (`??0Scene@Audio@@QEAA@XZ`, audio.cpp) by
// placement new. A holder that declares an `Audio::Scene` member gets the same
// constructor. CONCRETE, PUBLIC members. Retail's copy constructor
// (0x180050a50) is DEEP (two Array copies), so the class is not trivially
// copyable (B7-8).
//
// THE DESTRUCTOR IS THE IMPLICIT ONE, and that is retail's
// `??1Scene@Audio@@QEAA@XZ` (0x1800e0120): the AttenutedEffect range-destroy
// (FUN_180052110, which frees each slot's `positions` buffer), the +0x20
// buffer, then the +0x10 buffer — i.e. `~Array<AttenutedEffect>` then
// `~Array<Channel>`, reverse declaration order. The dtor door calls it.
//
// THREADS: GameUpdateLoop's slot-6 Load constructs one on the LOADER thread;
// the main thread updates it every frame.
// ============================================================================
class Scene {
public:
    // Fixed table sizes (the ctor's ArraySetSize calls) and the two handle spaces:
    // a Channel handle is index << 1 | generation << 9 (index 0..255); an
    // EffectSlot id is index << 1 | 1 (3D) | generation << 13 (index 0..4095). A
    // stored tag matches when (tag ^ handle) & the tag mask is 0.
    static constexpr int32_t  kChannelCount           = 0x100;         // channels
    static constexpr int32_t  kEffectSlotCount        = 0x1000;        // effects
    static constexpr int32_t  kHandleIndexShift       = 1;             // the index starts at bit 1 in both spaces
    static constexpr uint32_t kChannelIndexMask       = 0xff;          // channel index after >> 1 (MOVZX AL)
    static constexpr int32_t  kChannelGenerationShift = 9;             // channel generation bits 9..31
    static constexpr uint32_t kChannelTagMask         = 0xfffffe00u;   // TEST EAX,0xfffffe00
    static constexpr uint32_t kEffectIndexMask        = 0xfff;         // effect slot index after >> 1
    static constexpr int32_t  kEffectGenerationShift  = 13;            // effect generation bits 13..31
    static constexpr uint32_t kEffectTagMask          = 0xffffe000u;   // the effect id's validity mask
    static constexpr uint32_t kEffect3DBit            = 1u;            // bit 0 of a PlayEffect3D id
    // ------------------------------------------------------------------------
    // Scene::Channel — one 2D/general playback slot, 0x70 (112) bytes.
    // Pinned by Scene::PlayEffect (writes +0x00/+0x08/+0x10/+0x68), Scene::Update
    // (+0x18 volume, +0x1c sampleVolume, +0x6c "started" flag), Scene::StopEffect /
    // Scene::PauseEffect (+0x20..+0x5f eight platform-channel pointers, +0x60..
    // +0x67 eight per-sub-channel flag bytes — the default-construct pattern in
    // FUN_180051020 zeroes all of +0x00..+0x67 and +0x6c..+0x6f but sets +0x18
    // and +0x1c to 1.0f, matching StopEffect's identical reset pattern).
    // The NAME is descriptive: no mangled signature names this record.
    // ------------------------------------------------------------------------
    struct Channel {
        Object::ExternalPtr<SoundEffect> soundEffectRef; // +0x00
        uint32_t soundType;           // +0x08  Audio::Scene::SoundType
        uint32_t _pad0c;              // +0x0c  never read/written (alignment)
        System::Time startTime;       // +0x10
        float    volume;              // +0x18  default 1.0f
        float    sampleVolume;        // +0x1c  default 1.0f; Update stores the started variant's
                                      //        volume here (0x1800dfa9b) and mixes it with +0x18
        Device::AudioChannel* platformChannel[8]; // +0x20  up to 8 live Device channels
        uint8_t  channelFlags[8];     // +0x60  one byte per platformChannel slot
        uint32_t handleTag;           // +0x68  see the handle-encoding note above
        uint8_t  started;             // +0x6c  "has been given a start reference tick"
        uint8_t  _pad6d[3];           // +0x6d..+0x6f
    };

    // A {x,y,z,volume} instance record inside AttenutedEffect::positions.
    // Pinned by AddEmitter (builds this exact 16-byte value on the stack before
    // calling the array SetSize/insert) and MoveEmitter/SetEmitterVolume/
    // RemoveEmitter, which all stride the nested array in 16-byte steps.
    // GetAttenuatedVolume hands `&instance` to GetVolumeForPosition's
    // `const Vector3&` (0x1800dfde0), so the first twelve bytes are a Vector3.
    struct EmitterInstance {
        Vector3 position;
        float   volume;
    };

    // ------------------------------------------------------------------------
    // Scene::AttenutedEffect (retail's own spelling, from the mangled
    // `?GetAttenuatedVolume@Scene@Audio@@AEBAMAEBUAttenutedEffect@12@...`) — one
    // id-keyed slot in the 4096-entry table, 0x40 (64) bytes. Shared by the
    // ambient-emitter API (AddEmitter/..., which use `positions`) and the
    // 3D-effect API (PlayEffect3D/..., which use `pos`, `soundEffectRef`,
    // `channelHandle`). `EffectSlot` is this tree's older name for it.
    // ------------------------------------------------------------------------
    struct AttenutedEffect {
        Vector3             pos;               // +0x00  PlayEffect3D's own position; GetAttenuatedVolume
                                               //        hands `&effect` to a `const Vector3&`
        uint32_t            soundType;         // +0x0c
        Object::ExternalPtr<SoundEffect> soundEffectRef; // +0x10
        System::Time        startTime;         // +0x18
        uint32_t            channelHandle;     // +0x20  into the Channel table, 0 = none
        uint32_t            idTag;             // +0x24  see the handle-encoding note above
        float               baseVolume;        // +0x28  default 1.0f
        uint32_t            _pad2c;            // +0x2c
        Collection::Array<EmitterInstance> positions; // +0x30  (0x10 bytes: data/size/cap)
    };
    using EffectSlot = AttenutedEffect;

    // `?AW4SoundType@Scene@Audio@@` — a 4-byte enum passed in a register. Values
    // 0..4 index `paused[5]` and Update's volume table (1 UI, 2 music, 3 ambient,
    // 4 effect by the Configuration field each one reads; 0 is silent). The
    // ENUMERATOR NAMES are not in the binary, so none is invented.
    enum SoundType : int32_t {};

    // ---- PORTED members (bodies in src/engine/audio/) ---------------------
    // B7-5: `ExternalPtr<SoundEffect>` by value is a BARE register pointer; the
    // two `System::Time` by value in Update are REGISTER values (8 bytes).
    int32_t  GetEmitterCount(uint32_t id) const;                                   // 0x1800dec00
    void     MoveEmitter(uint32_t id, const Vector3& oldPos, const Vector3& newPos); // 0x1800dec40
    void     RemoveEmitter(uint32_t id, const Vector3& pos);                        // 0x1800decf0
    void     SetEmitterVolume(uint32_t id, const Vector3& pos, float volume);       // 0x1800ded60
    void     AddEmitter(uint32_t id, const Vector3& pos, float volume);             // 0x1800dedf0
    uint32_t FindEffect3D(Object::ExternalPtr<SoundEffect> effect);                // 0x1800dee70
    uint32_t PlayEffect3D(Object::ExternalPtr<SoundEffect> effect, SoundType type,
                          const System::Time& time, const Vector3& pos);           // 0x1800deeb0
    uint32_t PlayEffect(Object::ExternalPtr<SoundEffect> effect, SoundType type,
                        const System::Time& time);                                 // 0x1800def60
    void     Pause(int32_t soundType, bool paused);                                // 0x1800df070
    void     StopAll();                                                            // 0x1800df150
    void     StopEffect3D(uint32_t id);                                            // 0x1800df370
    void     PauseEffect(uint32_t handle);                                         // 0x1800df4a0
    void     StopEffect(uint32_t handle);                                          // 0x1800df550
    bool     IsEffectPlaying(uint32_t handle) const;                               // 0x1800df680
    bool     Update(System::Time delta, System::Time now, System::Random& rng,
                    const Vector3& listener, float radiusBias);                     // 0x1800df6c0
    void     Serialize(IO::Serializer& serializer);                                // 0x1800e0050
    // Retail's are PRIVATE (`AEBA`); public here because their doors call them.
    int32_t  FindFree3DEffect(Object::ExternalPtr<SoundEffect> effect, const System::Time& time,
                              const Vector3& pos) const;                           // 0x1800deae0
    float    GetAttenuatedVolume(const AttenutedEffect& effect, const Vector3& listener,
                                 float radiusBias) const;                          // 0x1800dfde0
    float    GetVolumeForPosition(const Vector3& pos, float maxDist, float minDist,
                                  const Vector3& listener) const;                  // 0x1800dfed0
    // The completion callback Update stamps into every Device channel it
    // starts (`Device::AudioChannel::onComplete`).
    static void OnSoundEnded(Device::AudioChannel* channel, void* scene, uint32_t tag); // 0x1800dffe0

    // ---- exported one-line accessor: faithful inline body, NOT REGISTERED --
    // `MOVSXD RAX,EDX ; MOVZX EAX,byte [RAX+RCX+0x30]` — unchecked index.
    bool IsPaused(int32_t soundType) const { return paused[soundType]; }          // 0x180050990

    // ---- exported, NOT PORTED, no caller: declared, deliberately undefined --
    Scene(const Scene& src);                                                       // 0x180050a50  (deep)
    Scene& operator=(const Scene& src);                                            // 0x180050ad0
    static void* operator new(std::size_t bytes);                                  // 0x1800508b0
    static void* operator new(std::size_t bytes, void* place);                     // 0x180050920
    static void  operator delete(void* p);                                         // 0x180050930
    void SetEffectVolume3D(uint32_t id, float volume);                             // 0x1800df000
    void SetEffectVolume(uint32_t handle, float volume);                           // 0x1800df040
    void UnpauseEffect(uint32_t handle);                                           // 0x1800df470  (Pause inlines it)
    void SoundEnded(Device::AudioChannel* channel, uint32_t tag);                  // 0x1800dff70  (OnSoundEnded inlines it)
private:
    int32_t FindFreeEffect() const;                                                // 0x1800debc0

public:
    // `??0Scene@Audio@@QEAA@XZ` — the arrays sized 256 / 4096 (audio.cpp).
    Scene();                                                                       // 0x1800e0160

    // ---- THE 0x38 BYTES ----------------------------------------------------
    int64_t  time;                    // +0x00  current System::Time ticks (set every Update)
    int32_t  playGeneration;          // +0x08  starts at 1; ++ on every PlayEffect/PlayEffect3D
    uint32_t _pad0c;                  // +0x0c
    Collection::Array<Channel>          channels; // +0x10  fixed at 256 entries after ctor
    Collection::Array<AttenutedEffect>  effects;  // +0x20  fixed at 4096 entries after ctor
    // One flag per Audio::Scene::SoundType (0..4). `bool`: Serialize writes the
    // five through the ReaderWriter's bool-array slot (+0x20, 0x1800e0050) and
    // IsPaused returns the byte as the bool.
    bool     paused[5];               // +0x30
    uint8_t  _pad35[3];               // +0x35..+0x37
};

static_assert(sizeof(Scene::Channel) == 0x70, "Audio::Scene::Channel must be 0x70 bytes");
static_assert(offsetof(Scene::Channel, _pad0c) == 0xC, "Channel::_pad0c");
static_assert(offsetof(Scene::Channel, _pad6d) == 0x6D, "Channel::_pad6d");
static_assert(offsetof(Scene::Channel, soundEffectRef)  == 0x00, "Channel::soundEffectRef @ +0x00");
static_assert(offsetof(Scene::Channel, soundType)       == 0x08, "Channel::soundType @ +0x08");
static_assert(offsetof(Scene::Channel, startTime)       == 0x10, "Channel::startTime @ +0x10");
static_assert(offsetof(Scene::Channel, volume)          == 0x18, "Channel::volume @ +0x18");
static_assert(offsetof(Scene::Channel, sampleVolume)    == 0x1c, "Channel::sampleVolume @ +0x1c");
static_assert(offsetof(Scene::Channel, platformChannel) == 0x20, "Channel::platformChannel @ +0x20");
static_assert(offsetof(Scene::Channel, channelFlags)    == 0x60, "Channel::channelFlags @ +0x60");
static_assert(offsetof(Scene::Channel, handleTag)       == 0x68, "Channel::handleTag @ +0x68");
static_assert(offsetof(Scene::Channel, started)         == 0x6c, "Channel::started @ +0x6c");

static_assert(sizeof(Scene::EmitterInstance) == 0x10, "EmitterInstance must be 16 bytes");
// BATCH 8b: the four offsets, pinned by the inner-array serialize
// (0x180051e00, src/engine/audio/audio_effectslot_serialize.cpp), which emits
// `kArrayFloat(n=3, &e[0])` then `kScalarFloat(&e[0]+0xc)` and strides 0x10.
// The widths are not inferred from the field types: each comes from the
// ReaderWriter vtable slot the body picks (Serializer.h `rw_vtbl`).
static_assert(offsetof(Scene::EmitterInstance, position) == 0x00, "0x180051e5a MOV R8,RBX (kArrayFloat n=3)");
static_assert(offsetof(Scene::EmitterInstance, volume)   == 0x0c, "0x180051e6b LEA RDX,[RBX+0xc]");

static_assert(sizeof(Scene::AttenutedEffect) == 0x40, "Audio::Scene::AttenutedEffect must be 0x40 bytes");
static_assert(offsetof(Scene::AttenutedEffect, _pad2c) == 0x2C, "AttenutedEffect::_pad2c");
static_assert(offsetof(Scene::AttenutedEffect, pos)            == 0x00, "pos @ +0x00");
static_assert(offsetof(Scene::AttenutedEffect, soundType)      == 0x0c, "soundType @ +0x0c");
static_assert(offsetof(Scene::AttenutedEffect, soundEffectRef) == 0x10, "soundEffectRef @ +0x10");
static_assert(offsetof(Scene::AttenutedEffect, startTime)      == 0x18, "startTime @ +0x18");
static_assert(offsetof(Scene::AttenutedEffect, channelHandle)  == 0x20, "channelHandle @ +0x20");
static_assert(offsetof(Scene::AttenutedEffect, idTag)          == 0x24, "idTag @ +0x24");
static_assert(offsetof(Scene::AttenutedEffect, baseVolume)     == 0x28, "baseVolume @ +0x28");
static_assert(offsetof(Scene::AttenutedEffect, positions)      == 0x30, "positions @ +0x30");

static_assert(sizeof(Scene::SoundType) == 4, "`W4SoundType` is a 4-byte enum in a register");
static_assert(sizeof(Scene) == 0x38, "Audio::Scene must be 0x38 bytes (exe MemAlloc(0x38,8) 0x14009706a)");
static_assert(offsetof(Scene, _pad0c) == 0xC, "Scene::_pad0c");
static_assert(offsetof(Scene, _pad35) == 0x35, "Scene::_pad35");
static_assert(alignof(Scene) == 8, "Audio::Scene aligns to 8");
static_assert(!std::is_polymorphic<Scene>::value, "Audio::Scene has no vtable");
static_assert(!std::is_trivially_copyable<Scene>::value, "B7-8: retail's copy ctor is deep");
static_assert(offsetof(Scene, time)           == 0x00, "Scene::time @ +0x00");
static_assert(offsetof(Scene, playGeneration) == 0x08, "Scene::playGeneration @ +0x08");
static_assert(offsetof(Scene, channels)       == 0x10, "Scene::channels @ +0x10");
static_assert(offsetof(Scene, effects)        == 0x20, "Scene::effects @ +0x20");
static_assert(offsetof(Scene, paused)         == 0x30, "Scene::paused @ +0x30");

// ============================================================================
// Audio::Configuration — 4 persisted volume sliders, 0x10 bytes, NON-polymorphic.
// Pinned by Save (0x1800dde10), which reads exactly these four offsets, by the
// ctor (0x1800ddeb0: 100/50/100/100) and by the eight accessors 0x18000d4a0..
// 0x18000d510 (one `MOV` each, UI/Music/Ambient/Effect at +0/+4/+8/+0xc).
// Allocated by `Engine::Startup` (`MemAlloc(0x10)` 0x1800dbd00) and built by
// the `Ctor_AudioConfiguration` DOOR (singleton_ctors.cpp, owner-retained): no
// C++ constructor is declared. `_singleton` (0x1801fe758) stays a data slot.
// ============================================================================
class Configuration {
public:
    // ---- PORTED members ----------------------------------------------------
    void Save();                                                   // 0x1800dde10
    bool Startup();                                                // 0x1800ddee0
    // `??1Configuration@Audio@@QEAA@XZ` 0x1800de0d0: clears `_singleton`, frees
    // nothing (the caller does). Its door calls it.
    ~Configuration();

    // ---- exported one-line accessors: faithful inline bodies, NOT REGISTERED
    int32_t GetUIVolume() const      { return uiVolume; }        // 0x18000d4a0
    int32_t GetMusicVolume() const   { return musicVolume; }     // 0x18000d4b0
    int32_t GetAmbientVolume() const { return ambientVolume; }   // 0x18000d4c0
    int32_t GetEffectVolume() const  { return effectVolume; }    // 0x18000d4d0
    void SetUIVolume(int32_t v)      { uiVolume = v; }           // 0x18000d4e0
    void SetMusicVolume(int32_t v)   { musicVolume = v; }        // 0x18000d4f0
    void SetAmbientVolume(int32_t v) { ambientVolume = v; }      // 0x18000d500
    void SetEffectVolume(int32_t v)  { effectVolume = v; }       // 0x18000d510

    // ---- exported, NOT PORTED, no caller: declared, deliberately undefined --
    static Configuration* Get();                                 // 0x18000d490 (reads _singleton)
    // ?_singleton@Configuration@Audio@@0PEAV12@EA (0x1801fe758) — the class static (ENDGAME B.3.9 ENG, B39_RULES D7).
    // The namespace's *PortInit redirects the minted storage row onto it, so
    // the name door, the address door and this member are the same bytes.
    // PUBLIC while foreign readers still reach the row through the doors.
    static Configuration* _singleton;
    void Shutdown();                                             // 0x1800ddea0
    Configuration& operator=(const Configuration& src);          // 0x18000d470

    int32_t uiVolume;       // +0x00
    int32_t musicVolume;    // +0x04
    int32_t ambientVolume;  // +0x08
    int32_t effectVolume;   // +0x0c
};
static_assert(sizeof(Configuration) == 0x10, "Audio::Configuration must be 0x10 bytes (MemAlloc 0x1800dbd00)");
static_assert(alignof(Configuration) == 4, "Audio::Configuration aligns to 4");
static_assert(!std::is_polymorphic<Configuration>::value, "Audio::Configuration has no vtable");
static_assert(offsetof(Configuration, uiVolume)      == 0x00, "GetUIVolume 0x18000d4a0: MOV EAX,[RCX]");
static_assert(offsetof(Configuration, musicVolume)   == 0x04, "GetMusicVolume: MOV EAX,[RCX+0x4]");
static_assert(offsetof(Configuration, ambientVolume) == 0x08, "GetAmbientVolume: MOV EAX,[RCX+0x8]");
static_assert(offsetof(Configuration, effectVolume)  == 0x0c, "GetEffectVolume: MOV EAX,[RCX+0xc]");

}  // namespace Audio

// ----------------------------------------------------------------------------
// The pre-B.3.7 spellings, kept as ALIASES of the classes (one definition per
// layout, B7-6) until their last user converts.
// ----------------------------------------------------------------------------
namespace RtAudio {
using Scene           = Audio::Scene;
using Channel         = Audio::Scene::Channel;
using EmitterInstance = Audio::Scene::EmitterInstance;
using EffectSlot      = Audio::Scene::AttenutedEffect;
using Configuration   = Audio::Configuration;
}  // namespace RtAudio

// ==== B3R G-Game (R5): Runtime bodies code/Game calls by name ====
// ---- audio/audio.cpp (??0Scene@Audio@@QEAA@XZ)
Audio::Scene* RtAudio_Scene_Ctor(Audio::Scene* self);
// ==== end B3R G-Game ====
