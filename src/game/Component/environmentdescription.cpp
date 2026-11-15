// src/game/Component/environmentdescription.cpp
// Clean reimplementation of EnvironmentDescription (entity description block).
//
// EnvironmentDescription (0x1b8 bytes) — per-map weather/lighting environment.
// Extends Object::External (0x20 base). Vtable at 0x14015e1f0. Parent "Object::External".
//   +0x20: ExternalPtr  (environment name/path ref)
//   +0x28: void* dynamic weather-entry array base (16-byte elements: {WeatherExt, ExtPtr})
//   +0x30: uint32 array count
//   +0x38..+0x1b8: 12 weather entries, each 0x20 bytes:
//     +0x00 Color(ambient) +0x04 Color(light) +0x08 Color(fog)
//     +0x0c float(fogNear) +0x10 float(fogFar) +0x14 float(fogMax)
//     +0x18 float(sunHeading) +0x1c float(sunPitch)   (the ToolKit's EnvironmentSetting)
//
// Factory  : MemAlloc 0x1b8 + External ctor + vtable + 12 weather-entry defaults.
// Serialize (vtable +0x28, 0x140094040), stream order:
//   ExternalPtr @ +0x20  (FUN_140002340 / ::Object::SerializeExternal)
//   int count   @ +0x30
//   [if loading: Reimpl_WeatherArray_Resize(+0x28, count)]
//   for each array entry (stride 16): WeatherExternalSerialize(entry) + ExternalPtr(entry+8)
//   12 weather entries @ +0x38 (each: 3 Color + 5 float, all 4-byte Serialize)
// GetVersion (vtable +0x18, 0x1400941b0): mov eax,7; ret -> version 7.
// Dtor (vtable +0x00, 0x140019190): MemFree(array @+0x28) + External::~External + free.
//
// The resize (0x1400198a0 Reimpl_WeatherArray_Resize) is called directly (B.3R); the two
// ExternalPtr serializes (0x1400196a0 Weather, 0x140002340) are ::Object::SerializeExternal<T>.
//
// Field knowledge harvested from engine/reimpl/environmentdescription.cpp.
// Decompiled from Application-x64.exe (READ-ONLY recon).
//
// ENDGAME B.3.8 K1: a REAL class (EnvironmentDescription.h) with a
// compiler-generated 9-slot vtable. The hand-built 11-slot table (slot 8 holding
// createInPlace, two guard slots past retail's end) and its
// RegisterExeAddr(0x14015e1f0) row are gone: nothing in the tree reads that
// address back (B38_RULES D5).

#include "Component/EnvironmentDescription.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "io/Serializer.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "Object/factory_registration.h"
#include <cstring>
#include <cstdint>
#include <new>
#include "Game/exe_bodies.h"
#include "Game/exe_constants.h"   // kRdata_14017214c (1.0f), kRdata_14017222c (255.0f bits)
#include "system/MemoryAllocator.h"
#include "Game/Weather.h"   // B8 RT2: ::Object::SerializeExternal<Weather> (the downcast needs it complete)
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)

namespace {
// The retail ctor stores raw IEEE bit patterns; this keeps them exact.
inline float FloatBits(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }
}  // namespace

// ============================================================
// EnvironmentDescription::WeatherEntry::Lerp -- 0x1400949f0
// ============================================================
// Retail first stores the record's defaults into the return slot; every field is
// then overwritten, so those stores are not spelled. The colours are written fog,
// light, ambient, each channel through 0..1 and back (truncated, CVTTSS2SI; the
// alpha byte comes out 0); then the three floats; the sun pair is lerped only with
// includeSun, else copied from this. Callers: WeatherSimulation::BlendLighting
// 0x140093ac0 (weather_sun.cpp). (Was the file-local LerpEnvironmentSetting there;
// moved by D.4 RESIDUE, row 1296 (5).)
namespace {
inline int32_t LerpChannel(uint32_t a, uint32_t b, int shift, float oneMinusT, float t) {
    const float k255 = FloatBits(kRdata_14017222c);   // 255.0f
    const float fa = static_cast<float>((a >> shift) & 0xFF) / k255;
    const float fb = static_cast<float>((b >> shift) & 0xFF) / k255;
    return static_cast<int32_t>((fa * oneMinusT + fb * t) * k255);
}
inline uint32_t LerpColor(uint32_t a, uint32_t b, float oneMinusT, float t) {
    const uint32_t r  = static_cast<uint32_t>(LerpChannel(a, b, 16, oneMinusT, t));
    const uint32_t g  = static_cast<uint32_t>(LerpChannel(a, b, 8, oneMinusT, t));
    const uint32_t bl = static_cast<uint32_t>(LerpChannel(a, b, 0, oneMinusT, t));
    return ((r << 8 | g) << 8) | bl;
}
}  // namespace

EnvironmentDescription::WeatherEntry
EnvironmentDescription::WeatherEntry::Lerp(const WeatherEntry& other, float t, bool includeSun) const {
    const float oneMinusT = kRdata_14017214c - t;   // 1.0f - t
    WeatherEntry out;
    out.fogColor     = LerpColor(fogColor, other.fogColor, oneMinusT, t);
    out.lightColor   = LerpColor(lightColor, other.lightColor, oneMinusT, t);
    out.ambientColor = LerpColor(ambientColor, other.ambientColor, oneMinusT, t);
    out.fogNear = oneMinusT * fogNear + t * other.fogNear;
    out.fogFar  = oneMinusT * fogFar + t * other.fogFar;
    out.fogMax  = oneMinusT * fogMax + t * other.fogMax;
    if (includeSun) {
        out.sunHeading = oneMinusT * sunHeading + t * other.sunHeading;
        out.sunPitch   = oneMinusT * sunPitch + t * other.sunPitch;
    } else {
        out.sunHeading = sunHeading;
        out.sunPitch   = sunPitch;
    }
    return out;
}

// ctor 0x1400941c0 (called by create 0x140019120 / createInPlace 0x140019160):
// `??0External@Object@@`, the stamp (the compiler's), then the defaults. The 12
// monthly entries all start from the same lighting.
EnvironmentDescription::EnvironmentDescription()
    : Object::External(),
      _environmentRef(nullptr) {   // _weatherArray: Array's three zero stores
    for (WeatherEntry& w : _months) {
        w.ambientColor     = 0xff595965;
        w.lightColor  = 0xffcbcbbc;
        w.fogColor     = 0xffbfbff2;
        w.fogNear     = 90.0f;
        w.fogFar       = 225.0f;
        w.fogMax = FloatBits(0x3f733333);   // 0.95f
        w.sunHeading     = 120.0f;
        w.sunPitch    = 65.0f;
    }
}

// dtor (slot 0 0x140019190 minus the stamps): an empty body; the member ~Array
// frees the weather array and zeroes its triple, then ~External.
EnvironmentDescription::~EnvironmentDescription() {}

// [5] 0x140094040.
bool EnvironmentDescription::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);

    ::Object::SerializeExternal<ComponentDescription>(ser, _environmentRef);   // ExternalPtr +0x20

    // count through a local (the resize below owns +0x30)
    int count = _weatherArray._size;
    ser.Serialize(count, nullptr);

    // On load: resize the dynamic weather-entry array.
    if (static_cast<int>(ser.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        Reimpl_WeatherArray_Resize(&_weatherArray, count);
    }

    for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
        ::Object::SerializeExternal<Weather>(ser, _weatherArray[i].weather);
        ::Object::SerializeExternal<ComponentDescription>(ser, _weatherArray[i].description);
    }

    // 12 inline weather entries @ +0x38, each 0x20 bytes. Retail 0x1400940f0..0x140094170:
    // the three packed colours through System::Color::Serialize (import 0x140151ee0,
    // the uint door), the five floats through Serialize(float&) (import 0x140152a60).
    for (WeatherEntry& w : _months) {
        ser.Serialize(w.ambientColor, nullptr);
        ser.Serialize(w.lightColor, nullptr);
        ser.Serialize(w.fogColor, nullptr);
        ser.Serialize(w.fogNear, nullptr);
        ser.Serialize(w.fogFar, nullptr);
        ser.Serialize(w.fogMax, nullptr);
        ser.Serialize(w.sunHeading, nullptr);
        ser.Serialize(w.sunPitch, nullptr);
    }
    return true;
}

// [6] 0x140019170: retail destroys and rebuilds in place: a virtual `this->~T()` (retail: slot 0, flags 0),
// then the ctor again on `this` (D9 #10).
Object::External* EnvironmentDescription::Reconstruct() {
    this->~EnvironmentDescription();
    return new (static_cast<void*>(this)) EnvironmentDescription();
}

// [7] 0x140001640: retail's shared body, a virtual `this->~T()` (retail: slot 0, flags 0) (D9 #10).
void EnvironmentDescription::Destruct() { this->~EnvironmentDescription(); }

// [8] 0x140019160: placement-new on `this` (folded with createInPlace). The
// hand-built table already held createInPlace here.
Object::External* EnvironmentDescription::Construct() {
    return new (this) EnvironmentDescription();
}

// create 0x140019120: MemAlloc(0x1b8, 8), then the ctor.
::Object::Base* EnvironmentDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(EnvironmentDescription), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) EnvironmentDescription());
}
// createInPlace 0x140019160.
::Object::Base* EnvironmentDescription::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) EnvironmentDescription());
}

DEFINE_EXE_TYPE(EnvironmentDescription, "Object::External")
