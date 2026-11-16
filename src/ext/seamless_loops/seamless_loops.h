// src/ext/seamless_loops/seamless_loops.h -- ext.seamless_loops (block SEAMLESS)
//
// NOT RETAIL. Every looping ambient sound the game ships (Crowd, River, Fire, Rain, Wind, Tornado, the
// biome beds) is xWMA, and the encoder pads the end of the file to a whole WMA frame: the decoded PCM
// ends in 10..45 ms of near-silence (docs/extensions/handins/CHATTER-DIAG.md §3). Retail's XAudio2 and our
// SDL voice both replay a looped buffer whole, so the murmur drops out and slams back in at every wrap.
//
// This extension rewrites the DECODED PCM of a LOOPING buffer once, at load (the user's own installed
// data, transformed in memory; nothing is shipped -- DESIGN.md §3 rule 7):
//   1. trim the codec tail: the trailing run of kTailWindowFrames windows whose RMS is below
//      kTailRatio x the buffer's own quiet level (the kQuietPercentile-th percentile of its 10 ms window
//      RMS), at most kMaxTrimFrames (one WMA frame);
//   2. fold the last kCrossfadeMs into the first kCrossfadeMs with an equal-power crossfade (sin / cos),
//      so the wrap joins the end of the sound to its start; the buffer gets kCrossfadeMs shorter.
// Equal power, not linear: the ambiences are noise-like (uncorrelated between the end and the start), and
// for uncorrelated signals sin^2 + cos^2 = 1 keeps the loudness constant through the fade, where a linear
// fade dips by 3 dB at its middle.
//
// WHERE IT RUNS. `Audio::SoundBuffer::DecodeAtLoad` (src/engine/audio/vtbatch7_soundbuffer_soundeffect.cpp),
// through the engine-side seam `Audio::SoundBuffer::_extLoopSeam` (null by default; DESIGN.md hook rule
// 5), and only when the buffer's own `_loopForever` is set: the loop is a property of the BUFFER
// (SoundBuffer +0x20, serialized before the payload), and the only thing that ever plays a buffer,
// Device::PlayPlatform, takes the loop count from that flag alone, so a looping buffer is never played
// once. The seam is installed at boot (Ext::InitFromSettings) when the row is on; it reads nothing live,
// so a toggle takes effect at the next start of the game.
//
// SAFE FAILURES. A sound whose own quiet level is 0 (>= 1 % of its 10 ms windows digitally silent)
// trims nothing and is only folded; a buffer under 2 x the crossfade, before or after the trim, is left
// exactly as decoded.
//
// Standard C++ only: tests/unit/ext_seamless_loops_test.cpp links this file on its own.
#pragma once

#include <cstdint>

namespace Ext {
namespace SeamlessLoops {

constexpr uint32_t kCrossfadeMs      = 50;    // the fold of the end into the start
constexpr uint32_t kTailWindowFrames = 64;    // the tail is measured in windows of this many frames
constexpr uint32_t kMaxTrimFrames    = 2048;  // at most one WMA frame (XwmaDecoder::FrameLength at 44.1 / 48 kHz)
constexpr float    kTailRatio        = 0.2f;  // a tail window is quieter than this x the quiet level
constexpr uint32_t kQuietPercentile  = 1;     // the quiet level: this percentile of the 10 ms window RMS

struct Result {
    bool     applied = false;       // false: the PCM is untouched (too short, bad input)
    uint32_t framesTrimmed = 0;     // the codec tail removed
    uint32_t crossfadeFrames = 0;   // kCrossfadeMs at the buffer's rate
    uint32_t newFrames = 0;         // the PCM's length afterwards (== frames when not applied)
    float    quietLevel = 0.0f;     // the reference the tail was measured against (S16 RMS)
};

// The crossfade length at `rate` Hz.
uint32_t CrossfadeFrames(uint32_t rate);

// Rewrites interleaved S16 `pcm` (`frames` frames of `channels`, 1 or 2) in place. Never applied to a
// buffer shorter than 2 x the crossfade (before or after the trim), nor with channels outside 1..2 or a
// zero rate; then the PCM is untouched and `applied` is false.
Result Transform(int16_t* pcm, uint32_t frames, uint32_t channels, uint32_t rate);

// The engine seam's body (Audio::SoundBuffer::ExtLoopSeam): Transform, then one `[SeamlessLoops]` line on
// stderr per buffer it looked at. Returns the PCM's length in frames afterwards.
uint32_t ApplyToLoopingBuffer(int16_t* pcm, uint32_t frames, uint32_t channels, uint32_t rate,
                              const char* name);

}  // namespace SeamlessLoops
}  // namespace Ext
