// src/game/Game/menuscene.cpp
// Clean reimplementation of MenuScene game logic.
//
// MenuScene (0x208 bytes) -- main menu scene controller.
// Extends Object::External (0x20 base). Vtable at 0x14015ede0.
//
// ENDGAME B.3.8 block LOOP: a REAL class (src/game/Game/MenuScene.h, field
// layout and retail slot map there) with a compiler-generated vtable. Gone:
// the 11-slot `g_MenuScene_Vtable_Standalone` (two over-read guards past
// retail's 9), its trampolines and stubs, the lazy EnsureResolved (a non-atomic
// first-call cache on the loader thread, B6-5: the External ctor/dtor dispatch
// slots it read are the compiler's base chain now) and the C entry points.
//
// Contains scene geometry (+0x28), dynamic arrays of scene objects
// (+0x30/+0x40/+0x50/+0x60), an external reference (+0x70),
// a weather reference (+0x80), an embedded camera controller at +0x88,
// atmosphere/lighting colors and fog parameters, plus two tree/map
// structures at +0x1C8 and +0x1E8.
//
// Also here: PlayerController's default ctor 0x140098800 (the class embedded at
// +0x88; the menu loop builds its own copy with it too).
//
// Decompiled from Application-x64.exe.

#include "Game/MenuScene.h"
#include "Game/PlayerController.h"
#include "Game/WorldGeometry.h"   // caller batch CB6: +0x28's class
#include "object/External.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "volume/Volume.h"
#include "Game/exe_bodies.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "math/Math.h"
#include "system/String.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch B
#include "io/Stream.h"
#include "object/Internal.h" // Object::detail::InternalMemAlloc / InternalMemFree
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

// Heap-overflow checkpoint (standalone diagnostic; no-op when
// BANISHED_REDZONE is unset).
#include "redzone_alloc.h"
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)
#include <type_traits>
#include "Game/Weather.h"   // B8 RT2: ::Object::SerializeExternal<Weather> (the downcast needs it complete)
#include "graphics/Particle.h"   // B8 RT2: ::Object::SerializeExternalArray<Graphics::Particle> (the downcast needs it complete)
#include "graphics/GraphicsMesh.h"   // B8 RT2: ::Object::SerializeExternal<Graphics::GraphicsMesh> (the downcast needs it complete)

// MenuScene::particles grows through 0x14001bf10 (stride 0x14, align 4, byte copy,
// the new tail zero-filled, no destroy): the trait row of MenuSceneParticle. Its
// default constructor is non-trivial (Vector3's) and implicit, so the row's init is
// value-initialisation, which zero-fills all 0x14 bytes first.
static_assert(alignof(MenuSceneParticle) == 4, "0x14001bf10 MemAlloc(.., 4)");
static_assert(std::is_trivially_copyable<MenuSceneParticle>::value, "0x14001bf10 byte copy");
static_assert(std::is_trivially_destructible<MenuSceneParticle>::value, "0x14001bf10 no destroy");
static_assert(!std::is_trivially_default_constructible<MenuSceneParticle>::value,
              "the trait row's init arm runs (value-initialisation zero-fills the new tail)");
#define RZ_CHECK(tag) RedzoneAlloc_CheckLinks(tag)

// M3.5 Path B session 8 — top-level Geometry serializers used by Serialize.

// One-shot log latches for Serialize (namespace scope, atomic: B6-5 — the scene
// loads on the loader thread).
namespace {
std::atomic<bool> s_serializeLogged{false};
std::atomic<bool> s_cameraLogged{false};
std::atomic<bool> s_particleLogged{false};
std::atomic<bool> s_atmosLogged{false};
std::atomic<bool> s_tailWarned{false};
}  // namespace

// ============================================================
// PlayerController::PlayerController() — 0x140098800
// ============================================================
// Transform::Transform (the member's) (0x14009880d), the target qword +0x1C
// and dword +0x24 zeroed (H7: three named stores), Transform3::Identity()
// (0x140098859), Frustum(near g_fUVScale = 1, far DAT_140172208 = 100, fovX 90,
// fovY 90) (0x14009888c), then LAST Camera::Camera(+0x34, frustum, identity,
// true) (0x14009889f): the camera is the init list's last entry. Yaw / pitch /
// distance are left unwritten, as retail's.
PlayerController::PlayerController()
    : transform(),
      target(0.0f, 0.0f, 0.0f),
      camera([] {
                 // Frustum ctor takes 4 Scalar* args in order (near, far, fovX,
                 // fovY) — the first two are stored as raw plane distances, the
                 // last two are interpreted as half-FOV angles (in degrees) for
                 // sin/cos (Volume::Frustum::Frustum, Runtime 0x1800fa1e0).
                 const Math::Scalar nearZ(1.0f), farZ(100.0f), fovX(90.0f), fovY(90.0f);
                 return ::Frustum(nearZ, farZ, fovX, fovY);   // 0x1800fa1e0
             }(),
             // ?Identity@Transform3@Math@@SA?AV12@XZ  retail 0x1800347f0.
             Math::Transform3::Identity(), true) {
}

// ============================================================
// MenuScene::MenuScene -- Constructor
// ============================================================
// Original: 0x1400a3d70
//
// External() and the 0x14015ede0 stamp are the compiler's; the members' own
// ctors zero the name, the four arrays and run 0x140098800 on the camera
// controller at +0x88; then the pointers, the atmosphere defaults (colors, fog,
// intensities), the +0x1B4 Vector4 (its ctor: zero) and the two tree structures.
MenuScene::MenuScene() {
    geometry = nullptr;      // +0x28: Geometry* (scene object)
    terrainMesh = nullptr;   // +0x70: ExternalPtr (typed reference)
    weather = nullptr;       // +0x80: ExternalPtr (Weather)

    // Atmosphere/lighting defaults (same as Weather initial values)
    ambientColor._value = 0xff595965;
    sunColor._value     = 0xffcbcbbc;
    skyColor._value     = 0xffbfbff2;
    fogStart         = 90.0f;       // 0x42b40000
    fogEnd           = 225.0f;      // 0x43610000
    fogDensity       = 0.95f;       // 0x3f733333
    sunIntensity     = 120.0f;      // 0x42f00000
    ambientIntensity = 65.0f;       // 0x42820000

}

// ============================================================
// MenuScene::Construct (vtable +0x40)
// ============================================================
// Original: 0x14001b800 — placement-new of the class on `this` (MSVC's
// placement new null-checks `this`, as the old slot did).
Object::External* MenuScene::Construct() {
    return ::new (static_cast<void*>(this)) MenuScene();
}

// ============================================================
// MenuScene::~MenuScene (body 0x1400a3970) / DeletingDtor (slot 0, 0x14001b830)
// ============================================================
// Restores the vtable (the compiler's), destroys the Geometry at +0x28
// (scalar delete via its vtable), cleans up the tree structures at +0x1E8 and
// +0x1C8; then the compiler frees the arrays at +0x60, +0x50, +0x40, +0x30
// (MemFree, data and count|capacity zeroed) and destroys the String at +0x20;
// External::~External is the compiler's chain.
MenuScene::~MenuScene() {
    // Destroy Geometry object at +0x28 via its vtable scalar deleting dtor.
    if (geometry) {
        delete geometry;  // flags=1 => free memory
    }

    // Then the compiler runs ~MenuSceneTreeMap (0x14001b860) on +0x1E8 and
    // +0x1C8, frees the four arrays and destroys the String: retail's order.
}

// ============================================================
// MenuScene::Reconstruct (vtable +0x30, Slot6)
// ============================================================
// Original: 0x14001b810. Destroys the object (slot 0, flags 0: no free) then
// re-constructs in place.
Object::External* MenuScene::Reconstruct() {
    this->~MenuScene();
    return ::new (static_cast<void*>(this)) MenuScene();
}

// [7] 0x140001640 — the shared body `MOV RAX,[RCX]; XOR EDX,EDX; JMP [RAX]`:
// slot 0 with flags 0 (destroy, no free).
void MenuScene::Destruct() { this->~MenuScene(); }

// ============================================================
// MenuScene::Serialize (vtable +0x28, Slot5)
// ============================================================
// Original: 0x1400a3630
//
// The outer Serializer (version 2), the name, the terrain Geometry (+0x28,
// built on load), the camera controller (+0x88), then the arrays, the
// GraphicsMesh reference, the atmosphere and the v2 tail.
bool MenuScene::Serialize(IO::Stream& stream) {

    // M3.5 Path B session 8 — Geometry deserialize sub-sequence.
    //
    // Mirrors the Geometry-allocation + serialize calls inside the original
    // MenuScene_Slot5 (0x1400a3630), specifically:
    //   0x1400a36c4: CALL 0x1400b00e0   ; Geometry ctor on freshly-allocated 0x230B buffer
    //   0x1400a36d5: CALL 0x1400b04d0   ; TradeWork_impl_04d0 (Geometry::Serialize body)
    //   0x1400a36f0: CALL 0x1400af1a0   ; TradeWork_impl_f1a0 (version-gated extra)
    //
    // We assume IsLoading by default (the standalone path only loads .rsc
    // payloads — never saves at this level). The original guards on
    // serializer's IsLoading flag plus `*(int*)(serializer + 8) == 0`,
    // but we don't have a parent serializer here; instead we trust the
    // stream's IsLoading state (read by the inner serializers).
    if (!s_serializeLogged.exchange(true)) {
        printf("[MenuScene::Slot5_Serialize] standalone Geometry-deserialize path "
               "(self=%p stream=%p)\n", (void*)this, (void*)&stream);
        fflush(stdout);
    }

    // (1) The OUTER Serializer (version=2) wrapping `stream`, then
    // String::Serialize on the name to advance the stream cursor past the
    // type-name marker that the file format encodes at the start of every
    // External payload. Without this prelude the cursor sits at ASCII
    // type-name bytes ("Gase"/"vers" etc.) and Geometry::Serialize reads garbage
    // as int counts → infinite-loop in Stream::Read.
    //
    // Mirrors original 0x1400a3656..0x1400a366f:
    //   ??0Serializer@IO@@QEAA@AEAVStream@1@H@Z(outer, stream, 2)
    //   ?Serialize@String@System@@QEAAXAEAVSerializer@IO@@@Z(&self+0x20, outer)
    // (2) Allocate + ctor a Geometry container at +0x28 if needed (after the
    // prelude, with or without a stream).
    auto buildGeometry = [this]() {
      if (geometry == nullptr) {
        if (!System::MemoryAllocator::_singleton) {
            printf("[MenuScene::Slot5_Serialize] MemAlloc unavailable — "
                   "skipping Geometry alloc (terrain will be missing)\n");
            fflush(stdout);
        } else {
            void* geom = System::MemoryAllocator::MemAlloc(0x230, 8);
            if (geom) {
                // Caller batch CB6: the real ctor (0x1400b00e0).
                geometry = ::new (geom) WorldGeometry();
            } else {
                printf("[MenuScene::Slot5_Serialize] MemAlloc(0x230) failed — "
                       "no Geometry container, skipping deserialize\n");
                fflush(stdout);
            }
        }
      }
    };

    IO::Stream& io = stream;
    IO::Serializer outer(io, 2);
    name.Serialize(outer);
    buildGeometry();

    // (3) The body serializer + version-gated extra. It MUST run: besides
    // building the terrain, it advances the stream cursor past the geometry
    // payload so the camera Transform (step 3.5) and the atmosphere/particle tail
    // (step 4) read from the correct offset. (geometry_serialize.cpp has the OOB
    // fix for the intermittent menu heap crash.)
    RZ_CHECK("Slot5 before geometry deserialize");
    if (geometry) {
        geometry->Serialize(io);                 // 0x1400b04d0
        // Only a version >= 1 stream carries the legacy batches: GetVersion of the
        // outer serializer, `CMP EAX,1; JL` past the call (0x1400a36da..0x1400a36f0).
        if (outer.GetVersion() >= 1)
            geometry->SerializeLegacyBatches(io);    // 0x1400af1a0
    }
    RZ_CHECK("Slot5 after geometry deserialize");

    // (3.5) The camera controller at +0x88 — retail calls 0x1400988c0 AFTER the
    // Geometry deserialize and BEFORE the atmosphere colors (inner Serializer
    // v=0: the transform, the target Vector3, yaw, pitch, distance). Without it
    // the menu camera is stuck at world origin.
    transform.Serialize(io);
    if (!s_cameraLogged.exchange(true)) {
        [[maybe_unused]] const Math::Transform& xf = transform.transform;
        printf("[MenuScene::Slot5_Serialize] camera Transform "
               "deserialized: pos=(%.2f, %.2f, %.2f) "
               "qrot=(%.3f, %.3f, %.3f, %.3f) +0x1c=(%.2f,%.2f,%.2f) "
               "+0x28=%.2f +0x2c=%.2f +0x30=%.2f\n",
               xf.translation.x, xf.translation.y, xf.translation.z,
               xf.rotation.x, xf.rotation.y, xf.rotation.z, xf.rotation.w,
               transform.target.x, transform.target.y, transform.target.z,
               transform.yaw, transform.pitch, transform.distance);
        fflush(stdout);
    }

    // (4) Tail of MenuScene::Slot5 — runs UNCONDITIONALLY in original
    // (outside the cVar2/IsValid encoding gate), on the outer Serializer, so the
    // cursor is in sync with the file. Maps to original 0x1400a3705..0x1400a3825:
    //   ?Serialize@Serializer@IO@@QEAAXAEAHPEBD@Z(outer, &count38, NULL)
    //   FUN_14001baf0(field30, count38)        ; resize stride-0x20 array
    //   FUN_14001bbf0(outer, count38, field30) ; serialize array elements
    //   ?Serialize@Serializer@IO@@QEAAXAEAHPEBD@Z(outer, &count48, NULL)
    //   FUN_140001bb0(field40, count48)        ; resize stride-0x08 array
    //   FUN_14001bcc0(outer, count48, field40) ; serialize ext-ptr array
    //   FUN_14001b8e0(&field70, outer)         ; single ext-ptr (GraphicsMesh)
    //   Math::Vector4::Serialize(field+0x1B4, outer)
    //   System::Color::Serialize(field+0x194, outer)  x3
    //   Serializer::Serialize_Float(outer, field+0x1A0, NULL) x5

    // ----- Array #1 (+0x30, stride 0x20) -----
    int count38 = meshes.GetSize();
    outer.Serialize(count38, nullptr);
    if (count38 > 0) {
        meshes.SetSize(count38);   // 0x14001baf0: Array<MenuSceneMesh>::SetSize (zero tail: MenuSceneMesh() value-initialises)
    }
    if (count38 > 0) {
        outer.Serialize(count38, meshes.GetData(), nullptr);   // 0x1400a373c -> 0x14001bbf0, IO::Serializer::Serialize<MenuSceneMesh>
    }

    RZ_CHECK("Slot5 after arr30 (stride0x20 instances)");

    // ----- Array #2 (+0x40, stride 0x08, GraphicsMesh exrefs) -----
    int count48 = meshResources.GetSize();
    outer.Serialize(count48, nullptr);
    if (count48 > 0) {
        Reimpl_Array_Resize_Stride08(&meshResources, count48);
    }
    if (count48 > 0) {
        ::Object::SerializeExternalArray<Graphics::GraphicsMesh>(outer, count48, meshResources.GetData());   // B9 RT2: retail 0x14001bcc0 is IO::Serializer::Serialize<ExternalPtr<T>> (R9 homed, never read); stays this call while the holder is a bare T* array (no retail proof to retype it)
    }

    RZ_CHECK("Slot5 after arr40 (GraphicsMesh exrefs)");

    // ----- Single ExternalPtr at +0x70 (GraphicsMesh) -----
    ::Object::SerializeExternal<Graphics::GraphicsMesh>(outer, terrainMesh);

    // ----- Atmosphere: Vector4 + 3 Colors + 5 Floats -----
    // ?Serialize@Vector4@Math@@QEAAXAEAVSerializer@IO@@@Z  retail 0x18001d360
    weatherTerms.Serialize(outer);   // +0x1B4
    ambientColor.Serialize(outer);
    sunColor.Serialize(outer);
    skyColor.Serialize(outer);
    // Color::Serialize from Runtime appears to write only the RGB
    // bytes and leave alpha at zero; Init defaults use 0xFF and the
    // renderer's uint32→Vector4 conversion treats the alpha byte as
    // the 4th component (which downstream lighting/fog math may
    // multiply against). Force opaque alpha so the deserialized
    // colors land in the same range as the Init defaults.
    ambientColor._value |= 0xFF000000;
    sunColor._value     |= 0xFF000000;
    skyColor._value     |= 0xFF000000;
    outer.Serialize(fogStart,         nullptr);
    outer.Serialize(fogEnd,           nullptr);
    outer.Serialize(fogDensity,       nullptr);
    outer.Serialize(sunIntensity,     nullptr);
    outer.Serialize(ambientIntensity, nullptr);

    // ----- v2-gated tail: Weather extPtr + +0x78 float + particle array
    //                       + particle resource exrefs ----------------
    // Maps to original 0x1400a3779..0x1400a3825 (the `if (1 < iVar3)` block).
    // We always run it since our outer is v2; the stream's version field
    // dictates serialize behavior internally.
    // Weather externalPtr at +0x80 (FUN_1400196a0).
    ::Object::SerializeExternal<Weather>(outer, weather);
    // Single float at +0x78 (the v2-only "scene-time" or similar).
    outer.Serialize(weatherIntensity, nullptr);

    // ----- Particle element array (+0x50, stride 0x14: 4 floats + 1 int) -----
    int count58 = particles.GetSize();
    outer.Serialize(count58, nullptr);
    if (count58 > 0) {
        // 0x14001bf10 (0x1400a387d) is Array<MenuSceneParticle>::SetSize: its trait row
        // (stride 0x14, align 4, byte copy, zero-filled new tail, no destroy).
        particles.SetSize(count58);
    }
    // Per-element serialize (inlined in original; not a separate helper).
    if (count58 > 0 && particles.GetData()) {
        for (int i = 0; i < count58; ++i) {
            MenuSceneParticle& elem = particles[i];
            outer.Serialize(elem.position.x, nullptr);
            outer.Serialize(elem.position.y, nullptr);
            outer.Serialize(elem.position.z, nullptr);
            outer.Serialize(elem.yaw, nullptr);
            outer.Serialize(elem.particleIndex, nullptr);
        }
    }

    RZ_CHECK("Slot5 after particle elem array (+0x50)");

    // ----- Particle resource exrefs (+0x60, stride 0x08, FUN_14001bff0) -----
    int count68 = particleResources.GetSize();
    outer.Serialize(count68, nullptr);
    if (count68 > 0) {
        Reimpl_Array_Resize_Stride08(&particleResources, count68);
    }
    if (count68 > 0) {
        ::Object::SerializeExternalArray<Graphics::Particle>(outer, count68, particleResources.GetData());   // B9 RT2: retail 0x14001bff0 is IO::Serializer::Serialize<ExternalPtr<T>> (R9 homed, never read); stays this call while the holder is a bare T* array (no retail proof to retype it)
    }

    if (!s_particleLogged.exchange(true)) {
        printf("[MenuScene::Slot5_Serialize] particles deserialized: "
               "count58=%d (elem array) count68=%d (resource exrefs)\n",
               count58, count68);
        fflush(stdout);
    }

    if (!s_atmosLogged.exchange(true)) {
        [[maybe_unused]] const Math::Vector4& v4 = weatherTerms;
        printf("[MenuScene::Slot5_Serialize] atmosphere deserialized: "
               "amb=0x%08x sun=0x%08x sky=0x%08x "
               "fogStart=%.2f fogEnd=%.2f fogDensity=%.3f "
               "sunInt=%.2f ambInt=%.2f "
               "vec4_at_1b4=(%.3f,%.3f,%.3f,%.3f) "
               "(arr30 count=%d arr40 count=%d)\n",
               ambientColor._value, sunColor._value, skyColor._value,
               fogStart, fogEnd, fogDensity,
               sunIntensity, ambientIntensity,
               v4.x, v4.y, v4.z, v4.w,
               count38, count48);
        fflush(stdout);
    }

    // (5) The outer Serializer's dtor runs at the scope's end, after this check
    // (the old body ran it before; nothing between reads the stream).
    RZ_CHECK("Slot5 exit");
    return true;   // retail returns AL=1
}

// ============================================================
// Reflection: create / createInPlace, the registration
// ============================================================
::Object::Base* MenuScene::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(MenuScene), 8);
    if (!p) return nullptr;
    Object::Base* obj = ::new (p) MenuScene();
    printf("[MenuScene::Factory] allocated %p (size=0x%x)\n",
           p, (unsigned)sizeof(MenuScene));
    return obj;
}
::Object::Base* MenuScene::CreateInPlaceObject(void* memory) {
    if (!memory) return nullptr;
    return static_cast<Object::Base*>(::new (memory) MenuScene());
}


DEFINE_EXE_TYPE(MenuScene, "Object::External")
