#pragma once
// ============================================================================
// src/game/Game/MusicPlayer.h — ENDGAME B.4 GAME-a
//
// The 0x18-byte music player at GameUpdateLoop+0x80 (m_musicPlayer): picks the
// next track from the town mood and the month when the current one ends.
//
// CLASS NAME: the profile build's header list has `MusicPlayer.h` (inventory D;
// the profile exe is not open, so the name is medium confidence).
// No vtable: a plain struct.
//
// RETAIL (Application-x64.exe):
//   Load 0x140096b00: MemAlloc(0x18, 4); +0x00 = -1; +0x08 =
//     Find("Game/MusicTracks.rsc") (type cell 0x1401c5a28); +0x10 = 0 (qword)
//   Update 0x1400a40e0: returns while Audio::Scene::IsEffectPlaying(+0x00);
//     reads the track list at +0x08 (+0x20 array, +0x28 count)
// ============================================================================

#include <cstdint>
#include <cstddef>

class MusicList;        // Game/MusicList.h: "Game/MusicTracks.rsc" (type cell 0x1401c5a28)
namespace Object { class External; }

class MusicPlayer {
public:
    uint32_t        effect;      // +0x00 the playing track's audio effect handle (ctor -1)
    uint8_t         _pad04[4];    // +0x04 not written
    MusicList*      trackList;    // +0x08 ExternalPtr (Load's Find)
    Object::External* lastTrack; // +0x10 the last played track's sound (ctor 0, qword); 0x1400a40e0 compares and stores it

    void Update();                // 0x1400a40e0 (body: unreg_lane5_musicplayer.cpp, B4 GAME-c)
};

static_assert(sizeof(MusicPlayer) == 0x18, "MusicPlayer is 0x18 (Load MemAlloc(0x18, 4))");
static_assert(offsetof(MusicPlayer, effect) == 0x00, "MusicPlayer::effect @ +0x00");
static_assert(offsetof(MusicPlayer, _pad04) == 0x04, "MusicPlayer +0x04");
static_assert(offsetof(MusicPlayer, trackList) == 0x08, "MusicPlayer::trackList @ +0x08");
static_assert(offsetof(MusicPlayer, lastTrack) == 0x10, "MusicPlayer +0x10");
