// src/game/Game/weather_sun.cpp
//
// WeatherSimulation::DrawUpdate (Application-x64.exe 0x1400938d0) and the two
// helpers it runs: the per-frame lighting blend 0x140093ac0 and the
// EnvironmentSetting lerp 0x1400949f0 (EnvironmentDescription::WeatherEntry::Lerp,
// Component/environmentdescription.cpp).
//
// Per rendered frame, GameUpdateLoop::Draw (0x140096158) calls
// `m_weather->DrawUpdate(*context)`. The body:
//   1. blends the environment's twelve monthly lighting records
//      (EnvironmentDescription +0x38, 0x20 each) for the current year time,
//      then blends every live rain / snow effect's own lighting (its Weather
//      resource's +0x60..+0x7C record) in by the effect's fade (+0xC0);
//   2. copies the four terrain weather floats (+0x1C..+0x28: snow coverage,
//      the two seasonal fades, the strongest effect's fade) into the Context
//      twice (+0x3B8 and the pixel staging +0x288) and dirties 0x10800;
//   3. Context::SetFog(fogNear, fogFar, {fog rgb, fogMax});
//   4. SunLight::Set(FromYawPitch(heading, -pitch), light rgb1, ambient rgb1)
//      on the world's sun (GameUpdateLoop +0x100).

#include "graphics/SunLight.h"
#include "video/Context.h"
#include "Game/exe_bodies.h"          // g_gameSettings
#include "Game/exe_constants.h"       // kRdata_*
#include "Game/GameUpdateLoop.h"
#include "Game/WeatherSimulation.h"
#include "Game/WeatherParticleGeometry.h"
#include "Game/Weather.h"
#include "Game/Calendar.h"
#include "Game/GameSettings.h"
#include "Component/EnvironmentDescription.h"
// EXT SEAM (NOT RETAIL, docs/extensions/handins/NOFOG.md): ext.no_fog (class A), Ext::NoFog::kFogMax
#include "ext/no_fog/no_fog.h"
#include "ext/registry.h"
#include "math/Math.h"
#include "system/Value.h"
#include <cstdint>

namespace {

using EnvironmentSetting = EnvironmentDescription::WeatherEntry;

inline float BitsF(uint32_t bits) { float f; __builtin_memcpy(&f, &bits, 4); return f; }

// One packed colour to {r, g, b, alpha} with each channel / 255.
inline Math::Vector4 UnpackColor(uint32_t c, float alpha) {
    const float k255 = BitsF(kRdata_14017222c);
    return Math::Vector4(static_cast<float>((c >> 16) & 0xFF) / k255,
                         static_cast<float>((c >> 8) & 0xFF) / k255,
                         static_cast<float>(c & 0xFF) / k255,
                         alpha);
}

}  // namespace

// 0x140093ac0 -- the frame's lighting: the two monthly records around the year
// time, lerped with the sun pair; then, when weather is shown, every live
// effect's Weather lighting lerped in by its fade, the sun pair kept.
EnvironmentSetting* WeatherSimulation::BlendLighting(EnvironmentSetting* out) const {
    Calendar* calendar = GameUpdateLoop::_singleton->m_dayCounter;   // +0x128
    const float seconds = calendar->yearTime.AsSeconds();
    int month = static_cast<int>((seconds / BitsF(kRdata_14017225c)) * BitsF(kRdata_1401721b8));
    if (month >= 12) month = 0;
    const float t = (seconds - static_cast<float>(month * 300)) / kRdata_140172234;
    *out = environment->_months[month].Lerp(environment->_months[(month + 1) % 12], t, true);   // 0x1400949f0

    if (g_gameSettings->showWeather) {   // GameSettings +0x16
        for (int i = 0; i < effects.GetSize(); ++i) {
            const WeatherParticleGeometry* effect =
                static_cast<const WeatherParticleGeometry*>(effects._data[i]);
            if (effect == nullptr) continue;   // retail's own test: a disabled slot is null
            const Weather* weather = effect->_descriptor;   // +0xC8
            EnvironmentSetting setting;
            setting.ambientColor     = weather->ambientColor._value;   // +0x60
            setting.lightColor  = weather->lightColor._value;     // +0x64
            setting.fogColor     = weather->fogColor._value;       // +0x68
            setting.fogNear     = weather->fogNear;               // +0x6C
            setting.fogFar       = weather->fogFar;                // +0x70
            setting.fogMax = weather->fogMax;                // +0x74
            setting.sunHeading     = weather->sunHeading;            // +0x78
            setting.sunPitch    = weather->sunPitch;              // +0x7C
            *out = out->Lerp(setting, effect->_intensity, false);   // 0x1400949f0, +0xC0
        }
    }
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/NOFOG.md):
    // ext.no_fog (class A): the fog maximum of the blended record is zero, so no fog is mixed into the
    // picture. Every reader of the frame's lighting goes through this function. With the row off
    // FogMax returns the same bits it was given, so the faithful record is unchanged.
    out->fogMax = Ext::NoFog::FogMax(out->fogMax, ::Ext::Ext().no_fog);
    return out;
}

// 0x1400938d0
void WeatherSimulation::DrawUpdate(Video::Context& context) {
    EnvironmentSetting setting;
    BlendLighting(&setting);
    Graphics::SunLight* sun = GameUpdateLoop::_singleton->m_sunLight;   // +0x100

    // The four terrain weather floats, to +0x3B8 and to pixel constant 4 (+0x288).
    context._userPosition.x = terrainWeatherParams.x;
    context._userPosition.y = terrainWeatherParams.y;
    context._userPosition.z = terrainWeatherParams.z;
    context._userPosition.w = terrainWeatherParams.w;
    context._pixelConstantStaging.pixel[4].x = terrainWeatherParams.x;
    context._pixelConstantStaging.pixel[4].y = terrainWeatherParams.y;
    context._pixelConstantStaging.pixel[4].z = terrainWeatherParams.z;
    context._pixelConstantStaging.pixel[4].w = terrainWeatherParams.w;
    context._dirty |= RtVideo::kDirtyGlobalConstants | RtVideo::kDirtyPixelConstants;

    // The fog colour's fourth channel is the record's fog maximum.
    context.SetFog(setting.fogNear, setting.fogFar,
                   UnpackColor(setting.fogColor, setting.fogMax));

    const Math::Vector4 ambient = UnpackColor(setting.ambientColor, 1.0f);
    const Math::Vector4 light   = UnpackColor(setting.lightColor, 1.0f);
    const Math::Scalar heading(setting.sunHeading);
    const Math::Scalar pitch(-setting.sunPitch);   // XORPS with the sign mask 0x1401722c0
    sun->Set(Math::Vector3::FromYawPitch(heading, pitch), light, ambient);
}
