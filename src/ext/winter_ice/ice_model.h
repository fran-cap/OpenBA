// ============================================================================
// src/ext/winter_ice/ice_model.h -- how far a lake has frozen, from the temperature (pure)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/WINTERICE.md. Plain arithmetic, no engine include: the unit
// test (tests/unit/ext_winter_ice_test.cpp) drives it with made-up weather.
//
// THE MODEL. The game's temperature is in degrees F, recomputed every tick (WeatherSimulation::Update
// 0x1400944e0); 32 is retail's own freezing point (kRdata_1401721d0, exe_constants.h). The ice is two
// fronts, both in the water's depth units (the frame copy's alpha: 0 at the bank, 1 about 1.5 world
// units down, every deeper pixel 1):
//
//   freeze  the ice reaches from the bank down to this depth. It grows with the cold (degree-seconds
//           below 32) and stops growing above 32. Past depth 1 (where every deep pixel sits) the ice
//           program adds a broad noise to the deep water's depth (up to kDeepSpread), so the middle
//           closes in drifting patches instead of all at once; at kFrozen and over, the whole lake is ice.
//   thaw    open water from the bank down to this depth: the shallow sides melt first while the deep
//           middle stays frozen. It grows with the warmth (degree-seconds above 32) once the lake has
//           any ice, and shrinks back if the cold returns (the edges freeze again).
//
// Ice covers the depths between the two. When the thaw front catches the freeze front the lake is
// open again and both reset. The rates are in degree-seconds of game time: on the Fair climate (a
// winter low near 20 F) the banks ice over within days of the first frost and the lake closes about a
// month in; the spring thaw takes a few weeks.
// ============================================================================
#pragma once

namespace Ext {
namespace WinterIce {

struct IceState {
    float freeze = 0.0f;   // depth units; >= kFrozen: the whole lake
    float thaw = 0.0f;     // depth units; 0 while it is cold
};

struct IceRates {
    float freezeDegreeSeconds = 1500.0f;   // cold to move the freeze front by one depth unit
    float thawDegreeSeconds = 1200.0f;     // warmth to move the thaw front by one depth unit
};

inline constexpr float kFreezingF = 32.0f;
// The deep water's patch noise in the ice program (ice_overlay.hlsl kDeepSpread): the deepest pixel
// sits at most this far past depth 1, plus the fronts' wander and soft edge.
inline constexpr float kDeepSpread = 0.5f;
inline constexpr float kFrozen = 1.65f;      // a freeze front at or past this: every pixel is ice
inline constexpr float kFreezeCap = 1.75f;   // the front stops here: a long winter needs a long thaw, not an endless one
inline constexpr float kMaxStepSeconds = 30.0f;   // a longer step (a load, a stall) is not integrated

// One step of `seconds` game seconds at `temperatureF`.
inline void Step(IceState& s, float temperatureF, float seconds, const IceRates& rates = IceRates{}) {
    if (!(seconds > 0.0f) || seconds > kMaxStepSeconds) return;
    if (temperatureF < kFreezingF) {
        const float d = (kFreezingF - temperatureF) * seconds / rates.freezeDegreeSeconds;
        s.freeze += d;
        if (s.freeze > kFreezeCap) s.freeze = kFreezeCap;
        s.thaw -= d;
        if (s.thaw < 0.0f) s.thaw = 0.0f;
        return;
    }
    if (s.freeze <= 0.0f) return;
    s.thaw += (temperatureF - kFreezingF) * seconds / rates.thawDegreeSeconds;
    // The ice that is left lies between the fronts; the deepest water is at kFrozen, so a freeze front
    // past it has nothing more to give once the thaw front reaches it.
    const float deepest = s.freeze < kFrozen ? s.freeze : kFrozen;
    if (s.thaw >= deepest) {
        s.freeze = 0.0f;
        s.thaw = 0.0f;
    }
}

// The state a town is given when the extension first sees it (a load, a new game, the row turned on):
// no history, so a guess from the temperature alone -- ice that fits a cold day, none on a warm one.
inline IceState Guess(float temperatureF) {
    IceState s;
    if (temperatureF < kFreezingF) {
        const float f = (kFreezingF - temperatureF) / 8.0f;
        s.freeze = f > kFreezeCap ? kFreezeCap : f;
    }
    return s;
}

// Any ice at all (the pass draws only then).
inline bool HasIce(const IceState& s) { return s.freeze > 0.0f && s.thaw < s.freeze; }

// Solid from bank to bank (phase 2's walkable test, docs/extensions/handins/WINTERICE.md section 5).
inline bool Solid(const IceState& s) { return s.freeze >= kFrozen && s.thaw <= 0.0f; }

}  // namespace WinterIce
}  // namespace Ext
