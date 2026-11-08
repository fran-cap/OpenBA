#pragma once
// ============================================================================
// src/engine/audio/SoundEffect.h — Audio::SoundEffect, 0x40 bytes, 9 slots
// ============================================================================
//
// ENDGAME B.3 step 7, block B4. The anonymous-namespace `SoundEffect` struct in
// src/engine/audio/vtbatch7_soundbuffer_soundeffect.cpp and the raw offsets
// audio_scene_update.cpp / audio.cpp read (+0x20, +0x28, +0x30, +0x38) MOVE
// here under the retail name (B6-8).
//
//   sizeof 0x40   `MOV EDX,0x40` in its own slot-0 vector deleting destructor
//                 (0x180050820) — B37_SLOTS.md section 1.
//   parent        "Object::External" (`?_ParentTypeName@SoundEffect@Audio@@`
//                 0x180050310, RULE B7-2) — Base, External, SoundEffect stamps.
//
// `??_7SoundEffect@Audio@@6B@` @ 0x180157ce0, NINE slots:
//   [0] +0x00 0x180050820  (unexported) vector deleting dtor — RT_DELDTOR row
//   [1] +0x08 0x180050340  ?TypeName@SoundEffect@Audio@@UEBAPEBDXZ
//   [2] +0x10 0x180050320  ?Type@SoundEffect@Audio@@UEBAIXZ
//   [3] +0x18 0x1800e0990  ?GetFileVersion@SoundEffect@Audio@@UEBAHXZ (MOV EAX,3)
//   [4] +0x20 0x1800e09a0  ?Compile@SoundEffect@Audio@@ — OVERRIDDEN (B7-1.7)
//   [5] +0x28 0x1800e0aa0  ?Serialize@SoundEffect@Audio@@UEAA_NAEAVStream@IO@@@Z
//   [6] +0x30 0x180050560  ?Reconstruct@SoundEffect@Audio@@UEAAPEAVExternal@Object@@XZ
//   [7] +0x38 0x180050640  ?Destruct@SoundEffect@Audio@@UEAAXXZ
//   [8] +0x40 0x1800505d0  ?Construct@SoundEffect@Audio@@UEAAPEAVExternal@Object@@XZ
//                          — introduced here (B7-1.6)
//
// FIELDS, named by the getters (read as bytes, retail 0x180050650..0x1800506b0):
//   +0x20 float  GetRadius       `MOVSS XMM0,[RCX+0x20]` — ctor default 1.0f
//                                (the ctor stores +0x20/+0x24 as ONE qword
//                                immediate 0x3f800000)
//   +0x24 float  GetInnerRadius  `MOVSS XMM0,[RCX+0x24]` — ctor default 0.0f
//   +0x28 Time   GetMaxLength    `MOV RAX,[RCX+0x28]; MOV [RDX],RAX` (retslot);
//                IsLooping 0x1800506b0 is `[RCX+0x28] == 0` — a zero maximum
//                length IS "loops forever" (Compile writes the 0)
//   +0x30 Array<Sample>  GetSoundCount `MOV EAX,[RCX+0x38]`; GetStartTime(i)
//                is `samples[i].startSeconds` at stride 0x18
//
// ENDGAME B.3 step 9, block ENG (B39_RULES.md D1/D2): CONCRETE. Retail's real
// public constructor (??0SoundEffect 0x1800e0810), its own slot 0, and the
// factory pair as its own statics: every live SoundEffect wears the COMPILER's
// table. The minted row, its RT_DELDTOR row 0x180050820, the ??1 door
// (deleting_dtor_leaves.cpp) and the eight slot doors are retired.
//
// PORTABILITY: no <windows.h>.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../object/External.h"
#include "../object/Internal.h"                // Object::detail::InternalMemFree
#include "../collection/Collection.h"

namespace System { class Filename; class Time; class Random; }

namespace Audio {

class SoundBuffer;

class SoundEffect : public Object::External {
public:
    // One playable variant of a sample, stride 0x18 (Scene::Update 0x1800df9fe
    // `LEA RBX,[RAX+RAX*2]` then *8; Serialize's element walk 0x180053390).
    // Names are the reader's (Scene::Update); none is retail-attested.
    struct Variant {
        Object::ExternalPtr<SoundBuffer> buffer;   // +0x00
        float pitchLow;                            // +0x08
        float pitchHigh;                           // +0x0c
        float volume;                              // +0x10
        float unusedPlayFactor;                            // +0x14  forwarded to PlayPlatform, never read
    };
    // One sample, stride 0x18 (Compile's outer loop `ADD RDI,0x18`).
    struct Sample {
        float    startSeconds;                     // +0x00  GetStartTime
        uint32_t _pad04;                           // +0x04
        Collection::Array<Variant> variants;       // +0x08  data, size@+0x10
    };

    // ??0SoundEffect@Audio@@QEAA@XZ 0x1800e0810 -- the REAL constructor. Its
    // stores are the ones retail's factory pair inlines (0x180050493..4d0):
    // External(), SoundEffect's vptr (the compiler's), then ONE qword
    // immediate 0x3f800000 over +0x20/+0x24 (radius 1.0f, inner radius 0.0f),
    // a zero +0x28 and the Array header +0x30/+0x38.
    SoundEffect() : Object::External(), _samples() {
        _radius = 1.0f;
        _innerRadius = 0.0f;
        _maxLength = 0;
    }

    // ---- the nine slots, in measured order ----
    // [0] 0x180050820 -- MSVC's VECTOR deleting destructor (`TEST DL,2`, the
    // eh-vector dtor over ~SoundEffect 0x1800e0880, element 0x40) (retail; here
    // the virtual destructor, B3X W1). Nothing `new[]`s one.
    // `??1SoundEffect@Audio@@UEAA@XZ` 0x1800e0880 -- VIRTUAL (B3X W1); The body
    // (vtbatch7_soundbuffer_soundeffect.cpp, moved from the retired
    // deleting_dtor_leaves.cpp door) destroys `_samples` through 0x1800516c0;
    // the compiler chains ~External.
    ~SoundEffect() override;

    // [1] 0x180050340 TypeName / [2] 0x180050320 Type, the static TypeId
    // 0x180050330, `_TypeName` 0x180050300 / `_ParentTypeName` 0x180050310 and
    // the factory pair 0x180050430 / 0x1800504f0: retail's macro (ENDGAME B.3.9
    // TI-REST, B39_RULES D6); the record is `DEFINE_TYPE`d in
    // src/engine/audio/audio_type_records.cpp.
    DECLARE_TYPE(SoundEffect)
public:
    int32_t       GetFileVersion() const override;             // [3] 0x1800e0990
    CompileResult Compile(const System::Filename& path) override; // [4] 0x1800e09a0
    bool          Serialize(IO::Stream& stream) override;            // [5] 0x1800e0aa0
    Object::External* Reconstruct() override;                  // [6] 0x180050560
    void          Destruct() override;                         // [7] 0x180050640
    virtual Object::External* Construct();                     // [8] 0x1800505d0 — B7-1.6

    // ---- exported one-line accessors: faithful inline bodies, NOT REGISTERED
    int32_t GetSoundCount() const        { return _samples._size; }              // 0x180050650
    float   GetStartTime(int32_t i) const { return _samples._data[i].startSeconds; } // 0x180050660
    float   GetRadius() const            { return _radius; }                     // 0x180050680
    float   GetInnerRadius() const       { return _innerRadius; }                // 0x180050690
    bool    IsLooping() const            { return _maxLength == 0; }             // 0x1800506b0

    // ---- exported, NOT PORTED, no caller: declared, deliberately undefined --
    // 0x1800506a0 — RETSLOT in retail (`?AVTime@System@@`).
    System::Time GetMaxLength() const;
    // 0x1800e08c0 — RETSLOT (`?AV?$ExternalPtr@VSoundBuffer@Audio@@@Object@@`);
    // Scene::Update inlines its variant pick instead of calling it.
    Object::ExternalPtr<SoundBuffer> GetSoundBuffer(int32_t sample, System::Random& rng,
                                                    float& volume, float& pitch,
                                                    float& unused) const;
    SoundEffect(const SoundEffect& src);                       // 0x1800506c0
    SoundEffect& operator=(const SoundEffect& src);            // 0x180050770
    static void* operator new(std::size_t bytes);              // 0x180050350
    static void* operator new(std::size_t bytes, void* place); // 0x1800503c0
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                     // 0x1800503d0
    // Retail's factory pair (B39_RULES D2), defined in
    // vtbatch7_soundbuffer_soundeffect.cpp. Placement spells `::new (p)`.
    // (Declared by DECLARE_TYPE above.)

public:
    // ---- the 0x20 bytes SoundEffect adds ----
    float                     _radius;       // +0x20  GetRadius
    float                     _innerRadius;  // +0x24  GetInnerRadius
    int64_t                   _maxLength;    // +0x28  GetMaxLength (System::Time ticks)
    Collection::Array<Sample> _samples;      // +0x30  data, size@+0x38, cap@+0x3c
};

static_assert(sizeof(SoundEffect::Variant) == 0x18, "SoundEffect variant stride 0x18");
static_assert(offsetof(SoundEffect::Variant, buffer) == 0x0, "Variant::buffer");
static_assert(offsetof(SoundEffect::Variant, pitchLow) == 0x8, "Variant::pitchLow");
static_assert(offsetof(SoundEffect::Variant, pitchHigh) == 0xC, "Variant::pitchHigh");
static_assert(offsetof(SoundEffect::Variant, volume)   == 0x10, "variant +0x10 volume");
static_assert(offsetof(SoundEffect::Variant, unusedPlayFactor) == 0x14, "variant +0x14");
static_assert(sizeof(SoundEffect::Sample) == 0x18, "SoundEffect sample stride 0x18");
static_assert(offsetof(SoundEffect::Sample, startSeconds) == 0x0, "Sample::startSeconds");
static_assert(offsetof(SoundEffect::Sample, _pad04) == 0x4, "Sample::_pad04");
static_assert(offsetof(SoundEffect::Sample, variants) == 0x08, "sample +0x08 variant table");
static_assert(offsetof(SoundEffect::Sample, variants) + offsetof(Collection::Array<SoundEffect::Variant>, _size) == 0x10,
              "sample +0x10 variant count");

static_assert(sizeof(SoundEffect) == 0x40,
              "Audio::SoundEffect is 0x40 bytes (MOV EDX,0x40 in slot 0, 0x180050820)");
static_assert(alignof(SoundEffect) == 8, "Audio::SoundEffect aligns to 8 (the vptr)");
static_assert(offsetof(SoundEffect, _radius)      == 0x20, "GetRadius 0x180050680 reads +0x20");
static_assert(offsetof(SoundEffect, _innerRadius) == 0x24, "GetInnerRadius 0x180050690 reads +0x24");
static_assert(offsetof(SoundEffect, _maxLength)   == 0x28, "GetMaxLength 0x1800506a0 reads +0x28");
static_assert(offsetof(SoundEffect, _samples)     == 0x30, "Serialize hands SetSize &this+0x30");
static_assert(offsetof(SoundEffect, _samples) + offsetof(Collection::Array<SoundEffect::Sample>, _size) == 0x38,
              "GetSoundCount 0x180050650 reads +0x38");
static_assert(offsetof(SoundEffect, _radius) == sizeof(Object::External),
              "SoundEffect's own bytes begin right after Object::External");

}  // namespace Audio
