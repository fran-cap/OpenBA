// src/game/Game/weather_sim.cpp
//
// Clean reimplementation of the per-tick Weather SIMULATION driver
// (Weather::ResetTime, exe 0x1400944e0) — the sub-simulation that evolves the
// world temperature and writes the four terrain "weather" params the terrain
// shader reads (snow coverage + seasonal fades + active-weather intensity).
//
// This is the piece that was missing in standalone: fn_Weather_ResetTime
// resolved to NULL (0x1400944e0 was never reimplemented), so the weather sim
// never ticked and GameUpdateLoop::Draw pushed hard-zero snow params into the
// terrain cbuffer (spring/clear). Wiring this in makes the ground snow track a
// real, animated temperature model.
//
// SCOPE (decompiled from Application-x64.exe, verified in Ghidra):
//   Weather::ResetTime has SIX logical parts. All are now ported — envRes loads
//   from Environment.rsc (GameUpdateLoop::Load Step 16), so the effect array
//   (weather+0x50, count weather+0x58 == weatherDescCount) is live and the
//   rain/snow particles spawn, integrate, and render:
//
//     Part 1  active-effect geometry cleanup + Update  — ported
//     Part 2  exponential smoothing (+0x2c,+0x44)      — ported
//     Part 3  Weather_helper_3810 temperature (+0x18)  — ported
//     Part 4  FUN_140093420 coverage/fades (+0x1c..)   — ported
//     Part 5  FUN_1400935f0 weather-type state machine — ported
//     impl_42c0 effect enable/disable (2x/tick)        — ported (WeatherEffect_SetEnabled)
//     Part 6  precipitation entity spawn               — ported (geometry)
//
//   The one piece still out of scope is the per-effect ambient AUDIO emitter
//   (the entry+8 audio template + Audio::Scene registration): audio was skipped
//   for the whole weather task. The visible falling rain/snow does not depend on
//   it. See weather_particles.cpp (particle geometry) and weather_sun.cpp (the
//   day-cycle lighting these effects tint).
//
// Weather sim struct (gameWorld+0x130, 0x78 bytes; ctor FUN_140093f60,
// env-bind FUN_140093e30):
//   +0x00 envRes (Environment weather descriptors; loaded from Environment.rsc)
//   +0x08 climateGen (ClimateGenerator resource; real, set by NewGameDialog)
//   +0x10 state    +0x14 intensityLevel (0..5)
//   +0x18 temperature (accumulated by Part 3)
//   +0x1c snowCoverage   +0x20 seasonalFadeA   +0x24 seasonalFadeB
//   +0x28 activeWeatherIntensity   (the four terrain-shader params)
//   +0x2c meanTemp (smoothed)   +0x30 meanTemp target (from climate+0x34)
//   +0x34 PerlinNoise (0x10 bytes, temperature noise; built by ctor)
//   +0x44 precipProb (smoothed)   +0x48 precipProb target
//   +0x50 activeEffect geometry array (ptr)   +0x58 count (== weatherDescCount)
//   +0x70 elapsed-time accumulator (int64)
//
// ClimateGenerator fields (real; ctor 0x1400930c0 defaults):
//   +0x34 temperatureBase=50  +0x38 tempVariation=30  +0x3c precip=5  +0x40 wind=10

// ENDGAME B.3.5 Video/Graphics caller batch: Graphics::Scene is a real class.
// This header MUST precede "resolve.h": resolve.h pulls <windows.h>, whose
// <wingdi.h> declares a global `Rectangle` FUNCTION that hides
// `Volume::Rectangle`'s class name in Volume.h's own static_asserts.
#include "graphics/Scene.h"
#include <new>
#include "graphics/Camera.h"
#include "Game/exe_bodies.h"   // B3R STORAGE: g_gameSettings
#include "Game/WeatherSimulation.h"
#include "Game/exe_constants.h"   // kRdata_140172100 / kRdata_140172194 (the noise)
#include "Game/WeatherParticleGeometry.h"
#include "Game/ClimateGenerator.h"
#include "Game/Calendar.h"
#include "Game/GameSettings.h"
#include "Component/EnvironmentDescription.h"
#include "Component/ComponentDescription.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Game/GameUpdateLoop.h"   // caller batch CB6: the world's named fields
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)
#include "utility/PerlinNoise.h"            // RtUtility_PerlinNoise_Ctor / _Generate
#include "crt/msvc_libm.h"                    // MsvcLibm::Sinf (the exe's sinf 0x140131c90)
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)
#include "Game/exe_bodies.h"

namespace {

// Exact .rdata float constants (bit patterns read from the exe at 0x14017xxxx).
inline float f32(uint32_t bits) { float f; __builtin_memcpy(&f, &bits, 4); return f; }

const float kSmoothIn  = f32(0x3a831200);  // DAT_1401720b4  ~0.001   (EMA new weight)
const float kSmoothOld = f32(0x3f7fbe77);  // DAT_140172148  ~0.99897 (EMA old weight)
const float kDeg2Rad   = f32(0x3c8efa35);  // DAT_1401720cc  pi/180
const float kPerlinDiv = 180.0f;           // DAT_140172220  (perlin time scale)
const float kKfSpan    = 300.0f;           // DAT_140172234  (keyframe span; day-of-year divisor)
const float kDayLen    = 3600.0f;          // DAT_14017225c  (seasonal cycle length)
const float k360       = 360.0f;           // DAT_140172238
const float kFreeze    = 32.0f;            // DAT_1401721d0  (freeze threshold)
const float kCovScale  = -1.0f;            // DAT_140172280  (coverage = -(temp-32))
const float kFadeA0    = 6.0f;             // DAT_14017219c
const float kFadeA1    = 1.5f;             // DAT_140172160
const float kFadeB0    = 0.5f;             // DAT_140172114
const float kFadeB1    = 7.5f;             // DAT_1401721a4
const float kFadeB2    = 2.0f;             // DAT_140172174

inline float saturate(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// Part 5/6 constants.
const float kColdThresh    = f32(0x42080000);  // DAT_1401721d8 = 34.0 (temp<=34 -> snow slot)
const float kPerlinPersist = 0.25f;            // DAT_140172100
const float kPerlinOctaves = 4.0f;             // DAT_140172194
const float k1_12          = f32(0x3daaaaab);  // DAT_1401720e0 = 1/12
const float kKfCount       = 48.0f;            // DAT_1401721e4

// Called by name (Entity/componententity_funcs.h): ComponentEntity_CtorFromDescription
// (0x1400c3720, clone an entity from a template desc) and EntityList_RegisterEntity_Impl
// (0x1400c48a0, RCX=entityList(gameWorld+0xe8), RDX=IBP*, R8D=typeId).

// Part 6 ambient-audio entity spawn: clone the environment's +0x20 template
// (an AmbientEnvironment carrier entity — biome wind/nature beds) and register it
// into the world entity list (gameWorld+0xe8). Mirrors the tail of 0x1400944e0.
void SpawnAmbientEntity(WeatherSimulation* w, GameUpdateLoop* gw) {
    EnvironmentDescription* envRes = w->environment;
    if (!envRes) return;
    ComponentDescription* templateDesc = envRes->_environmentRef;   // +0x20
    if (!templateDesc) return;
    if (!System::MemoryAllocator::_singleton)
        return;

    ComponentEntity* mem = static_cast<ComponentEntity*>(System::MemoryAllocator::MemAlloc(0x48, 8));
    if (!mem) return;
    ComponentEntity* entity = ::new (mem) ComponentEntity(templateDesc);   // 0x1400c3720

    ::Object::InternalPtr<ComponentEntity> ibp(entity);   // Object::InternalBasePtr (8 bytes)
    int typeId = static_cast<int>(templateDesc->_category);   // ComponentDescription +0x20
    EntityList* entityList = gw->m_entityList;
    if (entityList)
        entityList->RegisterEntity(&ibp, typeId);   // 0x1400c48a0

}

// Part 5 — FUN_1400935f0 weather-type state machine. Regenerates the temperature
// perlin + per-month type params at day boundaries, then picks the weather type
// (+0x10) from the climate probability table (climate+0x44, float[48] = 12
// months x 4 types) and steps the intensity level (+0x14, clamped 0..5).
void WeatherTypeStateMachine(WeatherSimulation* w, GameUpdateLoop* gw, float sec) {
    ClimateGenerator* c = w->climate;
    System::Random* rng = gw->m_random;
    if (!c || !rng) return;

    int kf = (int)((sec / kDayLen) * kKfCount);
    if (kf > 47) kf = 0;

    if (kf == 0 || kf == 0x18) {
        {
            uint32_t s1 = rng->Rand(0xffffffffu);
            uint32_t s2 = rng->Rand(0xffffffffu);
            RtUtility::PerlinNoise pn;
            RtUtility_PerlinNoise_Ctor(&pn, kPerlinPersist, kPerlinOctaves, (int)s2, (int)s1);
            w->noise = pn;
        }
        float accInt = 0.0f, accType = 0.0f;
        for (int k = 0; k < 12; ++k) {
            accInt  += rng->RandFloatTol() * k1_12;   // -> +0x48
            accType += rng->RandFloatTol() * k1_12;   // -> +0x30
        }
        w->field_0x30 = accType * c->windSpeed + c->temperatureBase;   // climate +0x40 / +0x34
        w->field_0x48 = accInt * c->_field104;                         // climate +0x104
    }

    float randf = rng->RandFloat();
    w->field_0x10 = 0;
    do {
        int type = w->field_0x10;
        int month = (kf + ((kf >> 31) & 3)) >> 2;
        // ClimateGenerator +0x44: 48 floats, four per month.
        float thresh = w->field_0x44 + c->_climateData[(size_t)(month * 4 + type)];
        if (randf < thresh) break;
        w->field_0x10 = type + 1;
    } while (w->field_0x10 < 3);

    int idx = w->field_0x10;
    if (idx == 0) w->field_0x14 += 1;
    else if (idx == 3) w->field_0x14 -= 1;
    if (w->field_0x14 > 5) w->field_0x14 = 5;
    else if (w->field_0x14 < 0) w->field_0x14 = 0;
}

// Part 6 chance — FUN_1400a9290(timer=+0x70, p=climate+0x108, rng). Consumes and
// zeros the elapsed-time accumulator; returns whether to spawn this tick.
bool SpawnChance(System::Time* timer, float p0, float p1, System::Random* rng) {
    if (p1 <= 0.0f) { timer->_ticks = 0; return false; }
    float scale = p0 * 12.0f * 300.0f;   // DAT_1401721b8 * DAT_140172234
    float elapsed = timer->AsSeconds();
    timer->_ticks = 0;
    float r = rng->RandFloat();
    return r < elapsed / (scale * p1);
}

// The effects are WeatherParticleGeometry (SetEffectEnabled builds them); +0xC0 is
// their fade value.
float EffectIntensity(Graphics::Geometry* geo) {
    return static_cast<WeatherParticleGeometry*>(geo)->_intensity;
}

}  // namespace

// ============================================================
// WeatherSimulation::WeatherSimulation (exe 0x140093f60)
// ============================================================
namespace {
// 0x140093faf..0x140093ff7: two stack Randoms (??0Random, no dtor), one draw
// each, then PerlinNoise(0.25, 4, second draw, first draw) in place at +0x34.
RtUtility::PerlinNoise NewWeatherNoise() {
    System::Random rng1;
    const uint32_t seed1 = rng1.Rand();
    System::Random rng2;
    const uint32_t seed2 = rng2.Rand();
    RtUtility::PerlinNoise noise;
    RtUtility_PerlinNoise_Ctor(&noise, kRdata_140172100, kRdata_140172194, (int)seed2, (int)seed1);
    return noise;
}
}  // namespace

// Row 754: the stores in retail's order. +0x00 / +0x08 / +0x10, the qword
// {+0x14 = 4, +0x18 = 0}, three qword zeroes over +0x1C..+0x33, the noise,
// then +0x44..+0x77 (the two Arrays' four zero qwords and the time) after it.
WeatherSimulation::WeatherSimulation()
    : environment(nullptr), climate(nullptr), field_0x10(0), field_0x14(4), temperature(0.0f),
      field_0x1C(), field_0x2C(0.0f), field_0x30(0.0f),
      noise(NewWeatherNoise()),
      field_0x44(0.0f), field_0x48(0.0f), seasonStep(-1),
      effects(), emitters(), disasterTime{0} {}

// ============================================================
// WeatherSimulation::Update (exe 0x1400944e0; the tree's old name for it was
// Weather::ResetTime) — snow/temperature sub-sim
//   this = gameWorld+0x130 (the weather sim struct)
//   time = the tick delta — only the deferred spawn path uses it
// ============================================================
void WeatherSimulation::Update(const System::Time* time) {
    // --- Game clock (seconds) — reads the sim clock at gameWorld+0x128 -> Time+8,
    //     exactly like the original helper_3810 / FUN_140093420. ---
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    if (!gw) return;
    Calendar* timeObj = gw->m_dayCounter;
    if (!timeObj) return;
    float sec = timeObj->yearTime.AsSeconds();

    // Weather-effects enabled setting (GameSettings::showWeather, +0x16).
    uint8_t weatherSetting = (g_gameSettings != nullptr) ? g_gameSettings->showWeather : 0;

    // --- Part 1: tick + cull the active weather effects. For each live effect,
    //     run its particle Update (vtable +0x08); if it has faded out (intensity
    //     0) or weather is disabled, remove it from the Graphics scene and
    //     destroy it. (Per-effect audio emitter volume is out of scope.) ---
    {
        int cnt = effects.GetSize();
        Graphics::Geometry** activeArray = effects._data;
        if (cnt > 0 && activeArray) {
            for (int i = 0; i < cnt; ++i) {
                Graphics::Geometry* geo = activeArray[i];
                if (!geo) continue;
                // Geometry slot 1: the particle Update(time).
                geo->Update(*time);
                float intensity = EffectIntensity(geo);
                if (intensity == 0.0f || weatherSetting == 0) {
                    {
                        Graphics::Scene* scene = gw->m_graphicsScene;
                        if (scene)
                            scene->RemoveGlobalObject(geo);
                    }
                    // Geometry slot 0: the scalar deleting dtor, free = 1.
                    delete geo;
                    activeArray[i] = nullptr;
                    // (audio IBP slot +0x60[i] stays null — no audio spawned)
                }
            }
        }
    }

    // --- Part 2: exponential smoothing of the mean-temp and precip targets. ---
    field_0x2C = field_0x30 * kSmoothIn + field_0x2C * kSmoothOld;
    field_0x44 = field_0x48 * kSmoothIn + field_0x44 * kSmoothOld;

    // --- Part 3: temperature accumulation (Weather_helper_3810) -> +0x18.
    //     temp = sin(seasonalPhase) * variation + mean  +  perlin * noiseAmp   ---
    {
        ClimateGenerator* c = climate;
        float mean = field_0x2C;
        float variation, noiseAmp;
        if (c) {
            variation = c->tempVariation;   // +0x38
            noiseAmp  = c->precipitation;   // +0x3C
        } else {
            // ClimateGenerator ctor defaults (0x1400930c0) — robust fallback if the
            // climate resource never bound (env-bind guards on climate != null).
            variation = 30.0f;
            noiseAmp  = 5.0f;
            if (mean == 0.0f) mean = 50.0f;   // temperatureBase default
        }
        float perlin = 0.0f;
        perlin = RtUtility_PerlinNoise_Generate(&noise, sec / kPerlinDiv, 0.0f);
        float phase = ((sec - kKfSpan) / kDayLen) * k360 * kDeg2Rad;   // radians
        float temp = MsvcLibm::Sinf(phase) * variation + mean;   // CALL 0x140131c90 (0x14009388e)
        temperature = perlin * noiseAmp + temp;
    }

    // --- Part 4: snow coverage + seasonal fades + active intensity (FUN_140093420). ---
    {
        float accum = temperature;
        float coverage = saturate((accum - kFreeze) * kCovScale);   // saturate(32 - temp)

        float doy = sec / kKfSpan;   // day-of-year progression (0..12 per cycle)

        float fadeA = (doy >= kFadeA0) ? saturate((doy - kFadeA0) / kFadeA1) : 0.0f;

        float fadeB;
        if (doy >= kFadeB0) {
            fadeB = (doy <= kFadeB1) ? 0.0f : saturate((doy - kFadeB1) * kFadeB2);
        } else {
            fadeB = 1.0f - saturate(doy / kFadeB0);
        }

        // Max intensity across the active weather effects (fade value geo+0xc0).
        float maxIntensity = 0.0f;
        int cnt = effects.GetSize();
        if (cnt > 0) {
            Graphics::Geometry** arr = effects._data;
            if (arr) {
                for (int i = 0; i < cnt; ++i) {
                    if (arr[i]) {
                        float v = EffectIntensity(arr[i]);
                        if (v > maxIntensity) maxIntensity = v;
                    }
                }
            }
        }

        field_0x1C.x = coverage;       // +0x1C
        field_0x1C.y = fadeA;          // +0x20
        field_0x1C.z = fadeB;          // +0x24
        field_0x1C.w = maxIntensity;   // +0x28
    }

    // --- Part 5: at each keyframe boundary, run the weather-type state machine
    //     (FUN_1400935f0) — picks the weather type (+0x10) + intensity (+0x14). ---
    {
        int kf = (int)((sec / kDayLen) * kKfCount);
        if (kf > 47) kf = 0;
        if (kf != seasonStep) {
            seasonStep = kf;
            WeatherTypeStateMachine(this, gw, sec);
        }
    }

    // --- Effect enable/disable (impl_42c0 loop). Two slots: 0 = warm precip
    //     (rain), 1 = cold precip (snow). Enable the slot whose temperature
    //     condition matches when weather is on and the sim is idle (state 0).
    //     State (+0x10) is re-read each iteration since SetEffectEnabled mutates it. ---
    {
        bool cold = temperature <= kColdThresh;   // temp <= 34
        for (int slot = 0; slot < 2; ++slot) {
            int state = field_0x10;
            int enable = (weatherSetting != 0 && state == 0 &&
                          ((cold ? 1 : 0) == slot)) ? 1 : 0;
            SetEffectEnabled(slot, enable != 0);
        }
    }

    // --- Elapsed-time accumulator (+0x70) — consumed by the Part-6 spawn chance. ---
    if (time)
        disasterTime._ticks += time->_ticks;

    // --- Part 6: ambient-spawn chance (FUN_1400a9290). Spawns the ambient-audio
    //     environment entity (the environment's +0x20 template) — the biome
    //     wind/nature bed — when the roll fires in warm clear weather. ---
    {
        ClimateGenerator* c = climate;
        uint32_t disasterBits = gw->m_gameFlags;
        System::Random* rng = gw->m_random;

        if (temperature > kColdThresh &&
            field_0x10 == 0 &&
            ((disasterBits >> 7) & 1) == 0 && c && rng) {
            if (SpawnChance(&disasterTime, c->_field108, c->_field10C, rng)) {   // climate +0x108 / +0x10C
                SpawnAmbientEntity(this, gw);
            }
        }
    }

}

