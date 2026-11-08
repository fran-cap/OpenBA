#pragma once
// ============================================================================
// src/game/Game/MusicList.h — ENDGAME B.3.8 block LOOP (B38_RULES D4, sec. 2.2)
//
// `MusicList` (0x30): the music-track playlist resource (Game\MusicTracks.rsc),
// a real `Object::External` subclass. Layout from factory 0x14001c260, dtor
// 0x14001c360, Serialize 0x1400a3e90.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md):
//   reflected "MusicList", parent "Object::External", table 0x14015ee40,
//   9 slots, sizeof 0x30.
//   [0] 0x14001c360 scalar deleting dtor   [1] 0x14001c250 TypeName
//   [2] 0x14001c240 Type                   [3] 0x1400a3f80 GetFileVersion `MOV EAX,1`
//   [4] IMPORT Object::External::Compile   [5] 0x1400a3e90 Serialize
//   [6] 0x14001c310 Reconstruct            [7] 0x140001640 Destruct
//   [8] 0x14001c2c0 Construct
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "object/External.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"

namespace Audio { class SoundEffect; }

// One track (stride 0x18). Serialize 0x1400a3e90 writes +0x08, +0x0c, +0x10,
// +0x14, then the +0x00 reference (0x1400019a0, ExternalPtr serialize); the
// resize 0x14001c3c0 seeds a new one with {null, 0.0f, 1.0f, 0, 11}. No field
// has a retail name.
struct MusicListTrack {
    Audio::SoundEffect* sound;    // +0x00 ExternalPtr<Audio::SoundEffect>: the track's audio resource (0x1400019a0's cell; B8 RT2)
    float             minStatus; // +0x08
    float             maxStatus; // +0x0C (new track: 1.0f)
    int32_t           minMonth; // +0x10
    int32_t           maxMonth; // +0x14 (new track: 11)
};
static_assert(sizeof(MusicListTrack) == 0x18, "stride 0x18 (0x14001c3c0)");
static_assert(offsetof(MusicListTrack, sound) == 0x0, "MusicListTrack::sound");
static_assert(offsetof(MusicListTrack, minStatus) == 0x08, "+0x08");
static_assert(offsetof(MusicListTrack, maxStatus) == 0x0C, "+0x0C");
static_assert(offsetof(MusicListTrack, minMonth) == 0x10, "+0x10");
static_assert(offsetof(MusicListTrack, maxMonth) == 0x14, "+0x14");

class MusicList final : public Object::External {
    DECLARE_EXE_TYPE(MusicList)

public:
    MusicList();                                                  // factory 0x14001c260 body

    ~MusicList() override;                                        // [0] 0x14001c360
    int32_t GetFileVersion() const override { return 1; }         // [3] 0x1400a3f80
    // [4] Compile: inherited (retail's slot is the import; the old table's was null)
    bool Serialize(IO::Stream& stream) override;                        // [5] 0x1400a3e90
    Object::External* Reconstruct() override;                     // [6] 0x14001c310
    void Destruct() override;                                     // [7] 0x140001640 this->~T()
    virtual Object::External* Construct();                        // [8] 0x14001c2c0

public:
    // The tracks (B.4 GAME-b: a Collection::Array; the factory's ctor zeroed its
    // header and the dtor 0x14001c360 is its trivial-element ~Array).
    Collection::Array<MusicListTrack> _entries;   // +0x20
};

static_assert(sizeof(MusicList) == 0x30, "MusicList is 0x30");
static_assert(offsetof(MusicList, _entries) == 0x20, "+0x20");
