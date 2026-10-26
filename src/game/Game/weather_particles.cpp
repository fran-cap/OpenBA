// src/game/Game/weather_particles.cpp
//
// Faithful reimplementation of the weather PARTICLE ENGINE (rain/snow) —
// the game-side Graphics::Geometry subclass that Banished uses for weather
// precipitation. Decompiled from Application-x64.exe (vtable @0x140160240).
//
// A weather effect is a 0xf0-byte Graphics::Geometry whose +0xc8 points at a
// Weather descriptor (Environment/*.rsc, 0x80 bytes — see weather.cpp). The
// geometry owns a particle array (+0xe0, stride 0x24) and an index buffer
// (+0xd0). Each frame it integrates particle positions, wraps them into a box
// tiled around the camera, and submits instanced billboard quads.
//
// Vtable (0x140160240, 9 slots) — mirrors the WorldGeometry pattern in
// geometry_draw.cpp:
//   +0x00 Dtor        (exe 0x140025060 -> body FUN_1400d3c10)
//   +0x08 Update      (exe 0x1400d2e60)  particle integration + spatial wrap
//   +0x10 Draw        (exe 0x1400d3120)  instanced billboards -> weather_particle_draw.cpp
//   +0x18 AddDrawItem (exe 0x1400d30b0)  Renderer::AddDrawItem
//   +0x20..+0x40      inherited Graphics::Geometry base virtuals
//
// ENDGAME B.3.8 block LOOP: the geometry is the REAL class
// `WeatherParticleGeometry` (src/game/Game/WeatherParticleGeometry.h) with a
// compiler vtable. Gone: the hand-built 9-slot `g_WeatherParticle_Vtable`, the
// lazy table resolves in the old ctor/dtor, the `??1Geometry` dtor door (the
// compiler's base chain runs the same body) and the GeomDraw_* forwarders for
// slots 4..8 (inherited).

// ENDGAME B.3.5 Video/Graphics caller batch: Graphics::Geometry and
// Graphics::Scene are real classes. Ahead of "resolve.h" — resolve.h pulls
// <windows.h>, whose <wingdi.h> declares a global `Rectangle` FUNCTION that
// hides `Volume::Rectangle`'s class name inside Volume.h's own static_asserts.
#include "Game/WeatherParticleGeometry.h"
#include "Game/Weather.h"          // caller batch CB6: the descriptor's fields
#include "Game/WeatherSimulation.h"
#include "Game/GameSettings.h"
#include "Component/EnvironmentDescription.h"
#include "Game/GameUpdateLoop.h"   // caller batch CB6: the game world's fields
#include "graphics/Scene.h"     // also brings Graphics::Geometry
#include "graphics/Camera.h"    // ENDGAME B.3.5 batch K1 — Graphics::Renderer
#include "graphics/Material.h"  // Graphics::MaterialInstance
#include "Game/exe_bodies.h"   // B3R STORAGE: g_gameSettings
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "math/Math.h"
#include "collection/Collection.h"
#include "object/Internal.h"     // Object::detail::InternalMemFree
#include <new>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)
#include "Game/exe_bodies.h"

// ============================================================
// Exe-resident float constants (bit-exact, embedded — .rdata not present in
// our standalone PE). Verified via Ghidra read of 0x14017xxxx.
// ============================================================
namespace {
constexpr float kHalf     = 0.5f;      // DAT_140172114
constexpr float kTickDt   = 0.016f;    // DAT_1401720c8 (prevPos seed delta)
constexpr float kBoundHi  =  1024.0f;  // DAT_140172250 (SetBounds max)
constexpr float kBoundLo  = -1024.0f;  // DAT_1401722ac (SetBounds min)
constexpr float kUVScale  = 1.0f;      // g_fUVScale

// GameSettings::showWeather (+0x16), 0 without settings.
inline uint8_t ShowWeatherSetting() {
    if (g_gameSettings == nullptr) return 0;
    return g_gameSettings->showWeather;
}
}  // namespace


// ============================================================
// Particle-array (re)alloc — port of FUN_140025090 (stride 0x24).
// Weather config calls this once on a fresh geometry (count 0 -> particleCount),
// so the grow/copy machinery collapses to: alloc, zero, set count. array header
// = { void* data(+0), int count(+8), int cap(+0xc) } at geo+0xe0.
//
// ENDGAME B.3.2 step 2: NOT converted. Retail 0x140025090 (re-decompiled
// 2026-09-13) is a plain rtcoll::SetSizeCore whose new-tail arm zeroes the
// 0x24-byte element (four qword stores + one dword) and whose prefix arm copies
// nine dwords. The body below instead memsets [0, newCount) of the buffer
// AFTER the grow — on an in-place grow or shrink from a non-zero count that
// also wipes the surviving prefix, which the reference never touches. Only
// the element row (`WeatherParticleElem`, below) is retail's; it is used by the
// geometry destructor. Reconciling this body with retail is a separate,
// deliberate change.
// ============================================================
namespace {
// The element is WeatherParticle (WeatherParticleGeometry.h); the geometry's
// destructor frees the array by hand (~Array's three stores).
}  // namespace

static void WeatherParticle_ArrayAlloc(RawArray<WeatherParticle>* h, int newCount) {
    int oldCount = h->size;
    if (newCount == oldCount) return;

    WeatherParticle* oldPtr = h->data;
    h->size = newCount;
    if (h->capacity < newCount) {
        int cap = newCount;
        if (newCount == oldCount + 1 && oldCount != 0) cap = newCount * 3;  // faithful grow rule
        h->capacity = cap;
        WeatherParticle* p = (System::MemoryAllocator::_singleton)
                      ? static_cast<WeatherParticle*>(System::MemoryAllocator::MemAlloc((size_t)cap * 0x24, 4))
                      : nullptr;
        h->data = p;
    }
    WeatherParticle* data = h->data;
    if (data && newCount > 0)
        std::memset(data, 0, (size_t)newCount * 0x24);
    if (oldPtr && oldPtr != data)
        System::MemoryAllocator::MemFree(oldPtr);
}

// ============================================================
// Geometry ctor — port of FUN_1400d3f10 (0xf0 bytes).
// ============================================================
// Graphics::Geometry() and the 0x140160240 stamp are the compiler's. Retail's
// stores run in declaration order (0x1400d3f39..0x1400d3f65): +0xC0, +0xC4,
// +0xC8, then the +0xD0 / +0xD8 qwords (`_indices`' Array ctor), then +0xE0 /
// +0xE8.
WeatherParticleGeometry::WeatherParticleGeometry()
    : _intensity(0.0f),
      _fadeDirection(1.0f),         // fade direction (+1 = fade in)
      _descriptor(nullptr),
      _indices() {                  // index buffer
    _particles.data = nullptr;      // particle array
    _particles.size = 0;
    _particles.capacity = 0;
}

// ============================================================
// Geometry dtor — port of FUN_1400d3c10 (+ scalar-deleting wrapper 0x140025060).
// ============================================================
WeatherParticleGeometry::~WeatherParticleGeometry() {
    // ENDGAME B.3.2 step 2: the particle array at +0xe0 is torn down by
    // hand (~Array of a trivial element: MemFree, then zero data and the
    // count / capacity qword: 0x1400d3c1b..0x1400d3c2f).
    System::MemoryAllocator::MemFree(_particles.data);
    _particles.data = nullptr;
    _particles.size = 0;
    _particles.capacity = 0;
    // B6 row 91: the +0xd0 index Array is then freed by its own ~Array, the
    // member's (retail's second MemFree and two qword zeroes; ~Array tests _data first since B6 row 104, as 0x18001c11f).
    // Graphics::Geometry::~Geometry: the compiler's chain.
}

// ============================================================
// Config — port of TradeWork_impl_3f80(geo, Weather* descriptor).
// Allocates + seeds the particle array and index buffer from the descriptor.
// ============================================================
// Caller batch CB6: the geometry's and the descriptor's fields are read by name.
// `geo` is a WeatherParticleGeometry (WeatherEffect_SetEnabled builds it just
// before; DebugUI's combo through the 0x1400d3f10 ctor door); `descriptor` is a
// Weather — the entry of EnvironmentDescription's weather array, serialized as
// ExternalPtr<Weather> (Reimpl_ExternalPtr_Serialize_Weather, type-validated).
void WeatherParticleGeometry::Configure(Weather* d) {
    _descriptor = d;      // +0xC8
    _intensity = 0.0f;    // +0xC0

    int particleCount = d->particleCount;   // Weather +0x28

    WeatherParticle_ArrayAlloc(&_particles, particleCount);   // +0xE0

    // Index buffer: 6 shorts (2 tris) per particle. B6 row 91: sized through
    // Array<uint16>::SetSize 0x14001c890 (0x1400d3fc3..0x1400d3fd5: data, size
    // and capacity; the old block is kept or freed by SetSize's own rule).
    _indices.SetSize(_descriptor->particleCount * 6);

    // B6 row 90: a LOCAL Random, constructed on the stack (0x1400d3fda LEA
    // RCX,[RBP-0x80]; 0x1400d3fde CALL [0x140152b50] ??0Random@System@@QEAA@XZ)
    // and never destroyed (no ~Random exists). Every draw below is RandFloatTol
    // ([0x140151400]) on it: the world Random (GameUpdateLoop::m_random) is not
    // advanced.
    System::Random random;

    // Normalized velocity direction (Weather.velocityDir @ +0x2c).
    float dir[3];
    {
        Math::Vector3 nd = d->velocityDir.Normalize();
        dir[0] = nd.x; dir[1] = nd.y; dir[2] = nd.z;
    }

    float boxSize = d->size;                 // Weather +0x48
    float speedBase = d->velocity;           // Weather +0x38
    float speedTol  = d->velocityTolerence;  // Weather +0x3c

    WeatherParticle* particles = _particles.data;   // +0xE0
    uint16_t* indices = _indices._data;             // +0xD0

    for (int i = 0; i < particleCount; ++i) {
        WeatherParticle& p = particles[i];   // stride 0x24

        // Random position within the box: randTol per axis * size * 0.5.
        float r0 = random.RandFloatTol();
        float r1 = random.RandFloatTol();
        float r2 = random.RandFloatTol();
        // Original order: Vector3(r2*size, size*r1, size*r0) then *0.5.
        p.position[0] = r2 * boxSize * kHalf;
        p.position[1] = r1 * boxSize * kHalf;
        p.position[2] = r0 * boxSize * kHalf;

        // Velocity = dir * (randTol*speedTol + speedBase).
        float rs = random.RandFloatTol();
        float speed = rs * speedTol + speedBase;
        p.velocity[0] = dir[0] * speed;
        p.velocity[1] = dir[1] * speed;
        p.velocity[2] = dir[2] * speed;

        // prevPos = pos - velocity * tickDt.
        p.previousPosition[0] = p.position[0] - p.velocity[0] * kTickDt;
        p.previousPosition[1] = p.position[1] - p.velocity[1] * kTickDt;
        p.previousPosition[2] = p.position[2] - p.velocity[2] * kTickDt;

        // Quad indices {base+0,+1,+2, +0,+2,+3}.
        // Unguarded, as retail's six stores (SetSize gave the block).
        {
            uint16_t base = (uint16_t)(i * 4);
            uint16_t* q = indices + (size_t)i * 6;
            q[0] = base;     q[1] = base + 1; q[2] = base + 2;
            q[3] = base;     q[4] = base + 2; q[5] = base + 3;
        }
    }

    // Identity transform + world bounds [-1024, 1024]^3.
    {
        Math::Transform tf = Math::Transform::Identity();
        SetTransform(tf);
    }
    {
        // ?SetBounds@Geometry@Graphics@@QEAAXAEBVBox@Volume@@@Z.
        ::Box box;
        box.min.x = kBoundLo; box.min.y = kBoundLo; box.min.z = kBoundLo;
        box.max.x = kBoundHi; box.max.y = kBoundHi; box.max.z = kBoundHi;
        SetBounds(box);
    }

    // Material layer bitmask: geo+0x30 = 1 << (material+0x20 & 0x1f).
    // Retail 0x1400d4323-0x1400d4338 reads [[desc+0x20]+0x20] with no test (row 960).
    Graphics::MaterialInstance* material = d->material;   // Weather +0x20
    uint32_t layer = material->_shaderClass;   // MaterialInstance +0x20
    _materialTypeFlags = 1u << (layer & 0x1f);   // Graphics::Geometry +0x30
}

// ============================================================
// Update (vtable +0x08) — port of FUN_1400d2e60(geo, const Time& dt).
// Integrates particle positions and wraps them into the [-half, half] box,
// carrying prevPos so motion-blur streaks stay continuous.
// ============================================================
void WeatherParticleGeometry::Update(const System::Time& timeRef) {
    const System::Time* time = &timeRef;
    // Caller batch CB6: the fields are read by name (the descriptor is a Weather,
    // see WeatherParticle_Config).
    if (!_descriptor) return;
    const Weather* const d = _descriptor;   // +0xC8

    float sec = time->AsSeconds();

    // Intensity fade toward the fade-direction target, clamped [0,1].
    float transition = d->transitionTime;   // Weather +0x5c
    float intensity = (transition != 0.0f)
        ? (sec / transition) * _fadeDirection + _intensity
        : _intensity;
    if (intensity > kUVScale) intensity = kUVScale;
    else if (intensity < 0.0f) intensity = 0.0f;
    _intensity = intensity;

    float box = d->size;   // Weather +0x48
    float half = box * kHalf;
    float negHalf = -half;

    int count = _particles.size;                  // +0xE8 count
    WeatherParticle* particles = _particles.data; // +0xE0
    if (!particles) return;

    for (int i = 0; i < count; ++i) {
        WeatherParticle& p = particles[i];
        // prevPos = pos (before integrating), then pos += vel*dt.
        p.previousPosition[0] = p.position[0];
        p.previousPosition[1] = p.position[1];
        p.previousPosition[2] = p.position[2];
        p.position[0] += sec * p.velocity[0];
        p.position[1] += sec * p.velocity[1];
        p.position[2] += sec * p.velocity[2];
        // Wrap each axis into [-half, half], shifting prevPos identically.
        for (int a = 0; a < 3; ++a) {
            while (p.position[a] > half)     { p.position[a] -= box; p.previousPosition[a] -= box; }
            while (p.position[a] < negHalf)  { p.position[a] += box; p.previousPosition[a] += box; }
        }
    }
}

// ============================================================
// AddDrawItem (vtable +0x18) — port of FUN_1400d30b0(geo, unused, Renderer*).
// ============================================================
void WeatherParticleGeometry::AddDrawItems(const Graphics::Scene& /*scene*/,
                                           Graphics::Renderer& rendererRef) const {
    if (ShowWeatherSetting() == 0) return;   // weather effects disabled in settings
    if (!_descriptor) return;                                                  // +0xC8
    const Graphics::MaterialInstance* material = _descriptor->material;       // Weather +0x20
    if (!material) return;
    rendererRef.AddDrawItem(
        this, material,
        0xffff, 0, 0.0f, kUVScale, 1, nullptr, 0);
}

// Draw (vtable +0x10) — the big instanced-billboard port is the member's body,
// defined in weather_particle_draw.cpp (caller batch CB6).

// ============================================================
// WeatherEffect_SetEnabled — port of ComponentEntity_impl_42c0(sim, slot, enable).
// Enables/disables one weather effect slot (0/1). ENABLE creates a particle
// Geometry from the envRes descriptor and adds it to the Graphics scene;
// DISABLE flags it to fade out. Audio-emitter spawn (the +8 template) is
// EXCLUDED per scope — the visible particle half is fully wired.
//   sim   = weather sim struct (gameWorld+0x130)
//   slot  = 0 or 1
//   enable= build effect if 1, fade out if 0
// ============================================================
void WeatherSimulation::SetEffectEnabled(int slot, bool enable) {
    Graphics::Geometry** activeArray = effects._data;   // +0x50 effect geometry array
    if (!activeArray) return;

    if (!enable) {
        // DISABLE: mark the effect to fade out (Update ramps intensity toward 0).
        Graphics::Geometry* g = activeArray[slot];
        if (g) {
            // The effects are WeatherParticleGeometry (built below and by 0x1400942c0).
            static_cast<WeatherParticleGeometry*>(g)->_fadeDirection = -1.0f;   // +0xC4
            field_0x10 = 3;   // sim state = fading
        }
        return;
    }

    // ENABLE: only build if this slot has no live effect yet.
    if (activeArray[slot] != nullptr) return;

    EnvironmentDescription* envRes = environment;   // +0x00
    if (!envRes) return;
    EnvironmentDescription::WeatherChoice* arrayBase = envRes->_weatherArray._data;   // +0x28
    if (!arrayBase) return;
    Weather* descriptor = arrayBase[slot].weather;
    if (!descriptor) return;

    if (!System::MemoryAllocator::_singleton) return;
    void* mem = System::MemoryAllocator::MemAlloc(0xf0, 8);
    if (!mem) return;
    WeatherParticleGeometry* geo = ::new (mem) WeatherParticleGeometry();
    activeArray[slot] = geo;

    geo->Configure(descriptor);

    // Add to the Graphics scene global object list (gameWorld+0x60).
    // B.3.5: the `fn_Graphics_Scene_AddGlobalObject` readiness term is gone; the
    // two singleton tests are object tests and stay.
    if (GameUpdateLoop::_singleton != nullptr) {
        Graphics::Scene* scene = GameUpdateLoop::_singleton->m_graphicsScene;
        if (scene)
            scene->AddGlobalObject(geo);
    }
    field_0x10 = 0;   // sim state = active
    // NOTE: the audio ambient entity (entry+8 template) is intentionally not
    // spawned here — audio is out of scope for this task.
}

// The C entry gameupdateloop_serialize.cpp calls. The null test is the old body's.
extern "C" void WeatherEffect_SetEnabled(WeatherSimulation* sim, int slot, int enable) {
    if (!sim) return;
    sim->SetEffectEnabled(slot, enable != 0);
}

// ============================================================
// WeatherResize — port of FUN_1400198a0. Stride-0x10 array resize (16-byte
// {Weather* descriptor, ExternalPtr audioTemplate} elements) used by
// EnvironmentDescription::Serialize to size the weather-descriptor array on
// load. Header {ptr(+0), count(+8), cap(+0xc)}. Copies existing, zero-fills new.
//
// ENDGAME B.3.2 step 2: now `Collection::Array<>::SetSize` over the
// `kSpec_WeatherEntry` row. Compared arm for arm against rtcoll::SetSizeCore
// and the retail body 0x1400198a0 (re-decompiled 2026-09-13): exact-equal early
// return, `_size` first, treble-on-single-append-from-non-empty, cap<<4 at
// align 8, two-qword copy of the prefix only when the block moved, two-qword
// zero init for the tail, old block freed only when it moved, no destroy.
// Every arm matched; only the `if (dst)` out-of-memory guard is gone.
// ============================================================
extern "C" void Reimpl_WeatherArray_Resize(Collection::Array<EnvironmentDescription::WeatherChoice>* hdr, int newCount) {
    // EnvironmentDescription +0x28, typed: the zero-initialised WeatherChoice's trait row
    // (stride 0x10, align 8, byte copy, zeroed tail, no destroy) is 0x1400198a0's.
    hdr->SetSize(newCount);
}
static_assert(sizeof(EnvironmentDescription::WeatherChoice) == 0x10, "0x1400198a0 stride");
static_assert(alignof(EnvironmentDescription::WeatherChoice) == 8, "0x1400198a0 align");
