// src/game/Game/unreg_lane5_musicplayer.cpp
// Code-fn wave LANE 5 — MusicPlayer::Update (exe 0x1400a40e0).
//
// WHAT WAS BROKEN WITH IT UNREGISTERED
// ------------------------------------
// GameUpdateLoop::Update step 7 (src/game/Game/gameupdateloop.cpp, the
// `post_simulation:` block) does
//
//     if (fn_MusicPlayer_Update) fn_MusicPlayer_Update(self->m_musicPlayer);
//
// with `fn_MusicPlayer_Update = ResolveAddr(Module::Exe, 0x1400a40e0)`. That
// address is unregistered, so the pointer is null and the call is SKIPPED
// every frame — a null-guard-and-SKIP consumer. The observable effect is that
// **background music never plays at all**: this function is the one and only
// writer of the music-player state and the one and only caller of
// Audio::Scene::PlayEffect for the music channel. Retail's sole caller is
// GameUpdateLoop::Update itself (Ghidra xrefs to 0x1400a40e0: one
// UNCONDITIONAL_CALL from FUN_140096380 plus its own .pdata unwind record).
//
// The Ghidra/our-tree name "AmbientSound::Update" (gameupdateloop.cpp:131) is a
// MISNOMER, already flagged in engine/types/GameUpdateLoop.h +0x80. The body is
// a music-track selector: pick a track whose [monthLo..monthHi] and
// [moodLo..moodHi] windows both contain the current month and the town mood.
//
// THE TRACK LIST (+0x08)
// ----------------------
// The retail body reads `*(int*)(musicPlayer[+0x08] + 0x28)` (the track count)
// with NO null check. GameUpdateLoop::Load step 27 (gameupdateloop_load.cpp) fills
// +0x08 with the "Game/MusicTracks.rsc" MusicList on every Load, as retail
// 0x1400972bb..0x140097321 does. (Review F-188: the Find used to be deferred to a
// once-per-process static here, so every game after the first had no list and no
// music.)
//
// FAITHFULNESS NOTE (an unguarded deref left in on purpose)
// ---------------------------------------------------------
// The mood normalisers come from citizen[0]'s description:
//     desc = citizen[0]->+0x18;
//     if (!IsDerivedFrom(desc, CitizenDescription)) desc = nullptr;
//     mood = (avg/desc->+0x70 + avg2/desc->+0xa4) * 0.5f;
// Retail dereferences `desc` unconditionally after that select — the decompile
// even carries the retail ASSERT text
// "entity.IsPtrValid() && entity->GetComponent<NaturalResourceComponent>()".
// Per CLAUDE.md we do NOT paper over that with a null check: if it ever fires,
// citizen[0] handed us a non-CitizenDescription and the fix is upstream.
//
// STRUCT LAYOUTS (from the 0x1400a40e0 decompile)
// -----------------------------------------------
// MusicPlayer (GameUpdateLoop+0x80, 0x18 bytes, MemAlloc'd by Load step 27):
//   +0x00 uint32  currentEffectHandle   (seeded -1)
//   +0x08 MusicList*                    (Load step 27's Find)
//   +0x10 uint64  lastTrackExternalPtr  (seeded 0)
// MusicList (src/game/Game/musiclist.cpp): +0x20 data, +0x28 count, stride 0x18
//   entry +0x00 ExternalPtr<SoundEffect>   +0x08 float moodLo   +0x0c float moodHi
//         +0x10 int   monthLo              +0x14 int   monthHi
//   (matches musiclist.cpp's default-init {+0x08=0, +0x0c=1.0f, +0x14=11}:
//    mood is normalised to 0..1 and months are 0..11.)
// GameUpdateLoop fields read: +0x78 Audio::Scene*, +0xd8 System::Random*,
//   +0xf0 citizen list (+0xa0 data / +0xa8 count, 8-byte InternalBasePtrs),
//   +0x128 dayCounter (its +0x08 is the accumulated System::Time).
// .rdata constants verified with read_memory:
//   DAT_140172114 = 0.5f    (mood average weight)
//   DAT_14017225c = 3600.0f (seconds per game year)
//   DAT_1401721b8 = 12.0f   (months per year)
//   DWORD_1401c7190 = the "CitizenDescription" type id (same global the
//   citizenai_* files resolve; see src/game/Component/citizenai_gather.cpp:63)

#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/GameUpdateLoop.h"   // caller batch CB6: the world's named fields
#include "system/Filename.h"
#include "system/String.h"
#include "Component/CitizenDescription.h"   // caller batch CB6: TypeId()
#include "object/External.h"   // ENDGAME B.3.3 step 6A
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)
#include "audio/Audio.h"       // ENDGAME B.3.7 callers (K4)
#include "Game/exe_bodies.h"
#include "Game/MusicPlayer.h"
#include "Game/MusicList.h"
#include "Game/Calendar.h"
#include "Entity/CitizenBehavior.h"
#include "Component/CitizenComponent.h"
#include "audio/SoundEffect.h"

namespace {

constexpr float kMoodAverageWeight = 0.5f;      // DAT_140172114
constexpr float kSecondsPerYear    = 3600.0f;   // DAT_14017225c
constexpr float kMonthsPerYear     = 12.0f;     // DAT_1401721b8

// DEFAULT-ON since the lane D close-out run of 2026-09-06, which is the first run that
// proved this function reaches the mixer:
//     [MusicPlayer] MusicTracks.rsc -> 00000271db93d780 tracks=15
//     [MusicPlayer] PLAY track=12/15 ext=271db912ee8 scene=00000271db7aa138 -> handle=1536
// (autostart new game, standalone_error.log:2565-2566). The MusicTracks.rsc Find (then
// a lazy one here, now Load step 27) resolved, all 15 tracks parsed, the month/mood filter selected one and
// Audio::Scene::PlayEffect returned a live handle.

// DWORD_1401c7190 is CitizenDescription's type-id cell (Table 1).
uint32_t CitizenDescriptionTypeId() {
    return CitizenDescription::TypeId();
}

} // namespace

// ============================================================
// MusicPlayer::Update — faithful port of exe 0x1400a40e0
// ============================================================

void MusicPlayer::Update() {
    if (GameUpdateLoop::_singleton == nullptr) return;

    MusicPlayer* mp = this;
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    Audio::Scene*   audioScene = gw->m_audioScene;

    // --- Early-out: a track is still playing on our handle. -----------------
    if (!audioScene) return;
    if (audioScene->IsEffectPlaying(mp->effect)) return;

    MusicList* trackList = mp->trackList;
    if (!trackList) return;   // port-only guard: retail reads the count unguarded (see the file header)

    // --- Town mood: average health and happiness over the citizen list, -----
    // each normalised by the corresponding max on citizen[0]'s description.
    CitizenBehavior* citizens = gw->m_citizenList;
    float    moodA = 0.0f, moodB = 0.0f;
    if (citizens) {
        int                count = citizens->citizens._size;
        Object::InternalPtr<CitizenComponent>* arr   = citizens->citizens._data;
        if (count > 0 && arr) {
            for (int i = 0; i < count; ++i) {
                CitizenComponent* c = arr[i].Get();
                moodA += c->health;
                moodB += c->happiness;
            }
            // Retail re-reads count and citizen[0] twice (once per normaliser);
            // both reads are identical, so hoist the description lookup.
            CitizenComponent* c0 = arr[0].Get();
            Description* descObj = c0->_description;
            uint32_t tid = CitizenDescriptionTypeId();
            CitizenDescription* desc = static_cast<CitizenDescription*>(descObj);
            if (!(tid && ::Object::IsDerivedFromNullSafe(descObj, tid))) desc = nullptr;
            // Faithful: retail derefs `desc` here with no null test (ASSERT path).
            moodA = (moodA / static_cast<float>(count)) / desc->maxHealth;
            moodB = (moodB / static_cast<float>(count)) / desc->maxHappiness;
        }
    }
    const float mood = (moodB + moodA) * kMoodAverageWeight;

    // --- Current month from the accumulated day-counter time. ---------------
    int month = 0;
    if (Calendar* dayCounter = gw->m_dayCounter) {
        float secs = dayCounter->yearTime.AsSeconds();
        int m = static_cast<int>((secs / kSecondsPerYear) * kMonthsPerYear);
        month = (m > 11) ? 0 : m;   // faithful: >11 wraps to 0, not to 11
    }

    // --- Collect every track whose month AND mood windows contain us. -------
    const int             trackCount = trackList->_entries._size;
    MusicListTrack* const tracks     = trackList->_entries._data;
    if (trackCount < 1 || !tracks) return;

    std::vector<Object::External*> matches;   // retail hand-rolls a MemAlloc'd grow array
    matches.reserve(4);
    for (int i = 0; i < trackCount; ++i) {
        const MusicListTrack* e = &tracks[i];
        const int   monthLo = e->minMonth;
        const int   monthHi = e->maxMonth;
        const float moodLo  = e->minStatus;
        const float moodHi  = e->maxStatus;
        if (monthLo <= month && month <= monthHi && moodLo <= mood && mood <= moodHi) {
            matches.push_back(e->sound);
        }
    }

    const int n = static_cast<int>(matches.size());
    int pick;
    if (n == 1) {
        pick = 0;
    } else if (n < 2) {
        return;                       // nothing eligible this month/mood
    } else {
        System::Random* rng = gw->m_random;
        if (!rng) return;
        pick = static_cast<int>(rng->Rand(static_cast<uint32_t>(n)));
        // Don't immediately repeat the last track: step one past it, wrapping.
        if (matches[static_cast<size_t>(pick)] == mp->lastTrack) {
            ++pick;
        }
        if (pick >= n) pick = 0;
        if (pick < 0)  return;
    }

    // Retail passes a zero-initialised System::Time by reference (SoundType 2).
    // The track's ExternalPtr is a SoundEffect (MusicListTrack::sound, typed External* by GAME-b).
    System::Time zeroTime{0};
    uint32_t handle = audioScene->PlayEffect(
        static_cast<Audio::SoundEffect*>(matches[static_cast<size_t>(pick)]),
        static_cast<Audio::Scene::SoundType>(2),
        zeroTime);
    // PROOF-OF-PLAY (lane D): the one line that says the music channel was actually
    // handed a track. Every track change logs; a run that never prints this has not
    // played music, whatever the rest of the trace says.
    fprintf(stderr, "[MusicPlayer] PLAY track=%d/%d ext=%p scene=%p -> handle=%u\n",
            pick, n, static_cast<void*>(matches[static_cast<size_t>(pick)]),
            static_cast<void*>(audioScene), handle);
    fflush(stderr);
    mp->effect = handle;
    mp->lastTrack = matches[static_cast<size_t>(pick)];
}
