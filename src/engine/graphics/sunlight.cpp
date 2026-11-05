// ============================================================================
// src/engine/graphics/sunlight.cpp — Graphics::SunLight   Wave 9 lane GFX2
// ============================================================================
//
// Three functions, two of them on the cutover residue list from attempt 14
// (docs/history/runtime_port/wave9_attempt14_unresolved.txt):
//
//   0x1801081f0  ??0SunLight@Graphics@@QEAA@XZ                    <- residue row
//   0x180107f10  ?Set@SunLight@Graphics@@QEAAXAEBVVector3@Math@@AEBVVector4@4@1@Z
//                                                                 <- residue row
//   0x180108160  ??1SunLight@Graphics@@UEAA@XZ                    (not on the list;
//                ported anyway — see "Why the dtor is here" below)
//
// All three mangled names were verified present in the shipped export table
// (tools/pe_exports.py over the shipped Runtime-x64.dll) BEFORE anything here
// was written, per reference_resolveexport_phantom_names.md.
//
// ---------------------------------------------------------------------------
// WHY THIS LANDS NOW, AND WHAT LANE GFX'S BLOCKER WAS
// ---------------------------------------------------------------------------
// Wave 9 lane GFX declined this pair (WAVE9_LANE_GFX.md §6.5) on one live
// reason: "the real blocker is two unported functions, and it is the ctor that
// needs them — ?PlatformConstruct@RenderBuffer@Video@@ (0x1800f4320) and
// ?PlatformDestruct@RenderBuffer@Video@@ (0x1800f4400) are exported and
// UNPORTED". Its recommended order was to land those two first so that porting
// SunLight closes a row rather than moving it one frame deeper. Both are now
// ported, in src/engine/video/buffers.cpp, as the fourth instantiation of Wave
// 9 lane W's Platform{Construct,Destruct} family. So the triage rule this
// project uses — "a port closes a cutover row only if every callee it needs is
// already satisfied on OUR side" — is now satisfied for the ctor:
//
//   Graphics::Geometry::Geometry     0x1800a4270   ported (scene.cpp, lane D)
//   Video::ImageBuffer::ImageBuffer  0x1800f2580   ported (buffers.cpp, lane C)
//   Video::ImageBuffer::Create       vtable +0x48  ported (buffers.cpp, lane C)
//   Video::RenderBuffer::PlatformConstruct         ported (buffers.cpp, THIS lane)
//   Video::RenderBuffer::PlatformDestruct          ported (buffers.cpp, THIS lane)
//   Video::RenderBuffer::Create      0x1800f3f10   ported (dynamic_resources.cpp)
//   MemoryAllocator::MemAlloc/MemFree              ported (system/allocator.cpp)
//   Graphics::Geometry::~Geometry    0x1800a41c0   (dtor only) ported (scene.cpp)
//
// ---------------------------------------------------------------------------
// THE FACT THAT CHANGES THE SHAPE OF ALL THREE BODIES: DeviceData SLOT 6 IS
// `xor al,al; ret`
// ---------------------------------------------------------------------------
// Both the ctor and `Set` open by asking the device a yes/no question:
//
//     dd = Video::Device::_singleton->deviceData;   // Device+0x100
//     cap = (*(bool(**)(void*))((*(char**)dd) + 0x30))(dd);   // vtable slot 6
//
// On the DX11 backend that call is a hardcoded FALSE, and this is verified two
// independent ways rather than assumed:
//
//   1. The shipped VideoDX11-x64.dll's DeviceData vtable is at 0x180018bb0;
//      the eight bytes at 0x180018be0 (slot 6) read `40 54 00 80 01 00 00 00`
//      = 0x180005440, and 0x180005440 is the two-byte body `32 c0 / c3`
//      — `xor al,al; ret`.
//   2. VideoDX11 is ALREADY REPLACED by src/render/dx11/ and is never loaded; our own
//      slot 6 is src/render/dx11/devicedata_stubs.cpp, whose header records the same
//      two bytes and which returns false.
//
// So on this backend, on both sides of the port:
//
//   * the ctor's capability-gated block is DEAD — `_colourTarget` (+0xF0) is
//     never built and stays null for the object's whole life;
//   * `Set`'s FIRST rebuild branch needs `cap == true`  -> unreachable;
//   * `Set`'s SECOND rebuild branch needs `cap == false && _colourTarget != 0`
//     -> unreachable, because nothing else ever writes +0xF0.
//
// **`SunLight::Set` is therefore exactly eleven dword copies on this backend,
// every call.** That is a stronger and differently-derived statement than
// WAVE9_LANE_GFX.md §6.5's, which said the rebuild branches are skipped "with
// the capability present and +0xf0 non-null (the state the ctor leaves)". The
// conclusion was right; the stated condition was the exact inverse of the live
// one, and the state the ctor leaves is +0xf0 NULL. Recorded because a later
// lane reading that sentence would look for the capability to be TRUE and find
// the opposite.
//
// The dead branches are still TRANSCRIBED here, faithfully, because the gate is
// a property of the BACKEND and not of SunLight: a DX9 device could answer
// true. They carry their own branch counters so the claim "unreachable" is
// measured on every diff run rather than asserted (see the [branch:] sites).
//
// ---------------------------------------------------------------------------
// WHY THE DTOR IS HERE THOUGH IT IS NOT ON THE RESIDUE LIST
// ---------------------------------------------------------------------------
// `??1SunLight@Graphics@@UEAA@XZ` is reached through the SunLight vtable, not by
// name, so a name-keyed residue scan never recorded it. At cutover that
// vtable is minted, and a minted vtable slot pointing at a DLL that is not
// there is the "mint is blind to what its own minted vtables READ" failure
// (reference_mint_population_rule_blind_to_its_own_output.md). It is also
// nineteen instructions, it is the exact teardown the ctor's construction
// needs, and the probe below needs a teardown for BOTH sides. Porting it is
// cheaper than arguing about it.
//
// ---------------------------------------------------------------------------
// operator new / operator delete
// ---------------------------------------------------------------------------
// The ctor uses three allocation sites and they are all the same thing:
//   0x18001e910  `operator new`    called;    lock alloc+0x640, Allocate(n,8), unlock
//   0x18001f2e0  `operator new`    inlined;   identical body
//   0x18001f360  `operator delete` called;    if (p) { lock, Free(p), unlock }
// Disassembled here rather than taken on trust: 0x18001e910 is
// `RDI = [_singleton]+0x640; Enter(RDI); Allocate([_singleton], size, 8);
// Leave(RDI)`, and 0x1800c7a50 (`?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z`)
// is byte-for-byte that with the alignment taken from EDX instead of an
// immediate 8. So `MemAlloc(n, 8)` / `if (p) MemFree(p)` reproduce all three
// exactly, and they route through the ported allocator instead of adding three
// more unregistered by-address doors.
// ============================================================================

#include "graphics/SunLight.h"
#include "video/Device.h"      // RtVideo::Device, RtVideo::RenderBuffer
#include "video/ImageBuffer.h" // ENDGAME B.3.9 VS: ::new (p) Video::ImageBuffer()
#include "video/CompanionData.h" // B.4: Video::DeviceData::Slot6
#include "system/MemoryAllocator.h"
#include <new>

#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#if BANISHED_TWEAK_OVERLAY
#include <atomic>
#endif

namespace {

// ---------------------------------------------------------------------------
// Dependencies, all members of the real Video classes (B.4: the wrappers that
// took `void*` receivers are gone):
//   `Video::RenderBuffer::Create` / `PlatformConstruct` / `PlatformDestruct`
//   `Video::ImageBuffer::Create` — vtable slot +0x48, eleven arguments after
//     `this`, counted against the mangled name
//     ?Create@ImageBuffer@Video@@UEAA_NW4ImageType@12@W4Usage@12@W4Format@12@IHH
//      W4FilterMode@12@W4AddressMode@12@44PEAV?$Array@V?$Array@E@Collection@@@Collection@@@Z
// ---------------------------------------------------------------------------

// `dd->vtable[6]()` — the capability gate, `Video::DeviceData::Slot6`. See the
// header block: this is `xor al,al; ret` on DX11 and therefore always false in
// this process, but the call is made rather than folded away, because the
// answer belongs to the device and not to us. LEFT: `Device::deviceData` is a
// `void*` in Device.h (video's header), so the one view of it is here.
bool DeviceSupportsColourShadowTarget() {
    return ::Video::Device::_singleton->deviceData->Slot6();
}

// `operator new` / `operator delete`, transcribed — see the header block.
void* OperatorNew(size_t n) { return System::MemoryAllocator::MemAlloc(n, 8); }
void  OperatorDelete(void* p) { if (p != nullptr) System::MemoryAllocator::MemFree(p); }

// ---------------------------------------------------------------------------
// The two composite steps the ctor and Set share, spelled once.
// ---------------------------------------------------------------------------

// `p = operator new(0x68); ib = p ? ImageBuffer::ImageBuffer(p) : nullptr`
//
// Retail keeps the ctor's RETURN value, not `p`. The first version of this
// function transcribed that literally — `return ImageBufferCtor()(p);` — and it
// CRASHED on the first diff run, in `ImageBufferCreate` reading a vtable at
// -1 (standalone_crash.log, RIP inside ImageBufferCreate+0x76, with R8=1/R9=7/
// RSI=0x40 — this file's own depth-target argument tuple).
//
// THE CAUSE IS A PORT-SEAM ABI HAZARD, worth stating rather than just fixing.
// MSVC returns `this` in RAX from a constructor, so retail's `MOV RCX,RAX` is
// correct against the SHIPPED body. But `??0ImageBuffer@Video@@QEAA@XZ` is
// PORTED, and our port is declared `void Video_ImageBuffer_Ctor(ImageBuffer*)`
// (src/engine/video/buffers.cpp:387) — a void-returning C++ function whose RAX
// is whatever the last expression left there. Reading a ported constructor's
// return value therefore reads garbage, on the DLL-present path AND at cutover.
// (`RtGraphics_Geometry_Ctor` does return `Geometry*`; the convention is not
// uniform across the tree, which is exactly why this must not be relied on.)
//
// `p` and the returned `this` are the same pointer by construction, so using
// `p` is byte-faithful to what retail computes while being independent of how
// any port spells its signature.
Video::ImageBuffer* NewImageBuffer() {
    void* p = OperatorNew(0x68);
    if (p == nullptr)
        return nullptr;
    // ENDGAME B.3.9 VS: the ??0 door and the minted row are retired. The
    // placement-new expression's value IS `p`.
    return ::new (p) Video::ImageBuffer();
}

// `rb = operator new(0x30); if (rb) { rb[0]=rb[8]=rb[0x10]=0;
//                                      RenderBuffer::PlatformConstruct(rb); }`
// This is `Video::RenderBuffer::RenderBuffer` (0x1800f3ee0) INLINED into both
// call sites — the zeroing of colorBuffer/depthBuffer/width+height followed by
// PlatformConstruct — and it is transcribed as the inline that it is rather
// than redirected to the ctor export, because the shipped bytes at both sites
// are the inline.
//
// The raw allocation is typed once as the RenderBuffer the inlined ctor builds
// in it (the class's own ctor is not what retail runs here).
Video::RenderBuffer* NewRenderBuffer() {
    Video::RenderBuffer* const rb = static_cast<Video::RenderBuffer*>(OperatorNew(0x30));
    if (rb == nullptr)
        return nullptr;
    rb->colorBuffer = nullptr;   // +0x00
    rb->depthBuffer = nullptr;   // +0x08
    rb->width  = 0;              // +0x10 \ one qword store in retail
    rb->height = 0;              // +0x14 /
    rb->PlatformConstruct();
    return rb;
}

// `if (rb) { RenderBuffer::PlatformDestruct(rb); operator delete(rb); }`
void DeleteRenderBuffer(Video::RenderBuffer* rb) {
    if (rb == nullptr)
        return;
    rb->PlatformDestruct();
    OperatorDelete(rb);
}

// `if (ib) (*ib->vtable[0])(ib, 1)` — retail's deleting destructor
// (reference_phantom_dtor_mangling_qeaa_vs_ueaa.md: slot 0 is ??_E, not ??_G).
// B3X W0 audit: by name, through the object's own class (Object::Base root);
// B3X W1: that call is `delete`.
void DeleteImageBuffer(Video::ImageBuffer* ib) {
    if (ib == nullptr)
        return;
    delete ib;
}

// The two float constants the ctor's bounds block uses, LIFTED AS RAW BITS and
// not retyped from a decimal (reference_crt_coefficients_must_be_lifted_not_retyped.md).
//   0x18016173c = 00 24 74 49 = 0x49742400
//   0x180161778 = 00 24 74 c9 = 0xc9742400
float BitsToFloat(uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }
const float kBoundsMax = BitsToFloat(0x49742400u);   // DAT_18016173c
const float kBoundsMin = BitsToFloat(0xc9742400u);   // DAT_180161778

}  // namespace

// ============================================================================
// Graphics::SunLight::Set(const Math::Vector3& dir, const Math::Vector4& sun,
//                         const Math::Vector4& ambient)   — 0x180107f10
//
// ELEVEN DWORD COPIES, then two rebuild branches that this backend never takes.
// The copies are `MOV EAX,[src] / MOV [this+off],EAX` in the shipped body — no
// float arithmetic at all, so there is no association order to get wrong and no
// bit-exactness risk here (WAVE9_LANE_GFX.md §6.5 finding 1, confirmed).
//
// THE ORACLE, and its one refusal. On the copy-only path this is a pure写-once
// function of three inputs that our body does not touch, so it replays exactly:
// snapshot the eleven dwords, run ours, read them back, restore the snapshot,
// run the shipped body, read again, compare. The three SOURCE vectors are the
// caller's and are never written by either side, so the aliasing trap in
// reference_oracle_must_snapshot_aliased_inputs.md does not apply — but the
// DESTINATION is `this`, which is live state, which is why the snapshot is
// restored before the shipped body runs rather than after.
//
// If either rebuild branch is taken the diff is REFUSED, not faked: replaying
// would destroy and rebuild GPU resources a second time and the "comparison"
// would be between two different objects. That call is counted on its own site
// so a refusal is visible instead of silently inflating `calls=`.
// ============================================================================

namespace Graphics {

// ============================================================================
// NOT RETAIL -- ext.max_shadow_res (SunLight.h has the seam's contract;
// docs/extensions/handins/FAA-SHADOW.md, FAA-SHADOW2.md). Retail creates the sun
// map 0x800 raw with flag bit 6, and Width() shifts that by
// `Configuration::shadowScalar` (High / Medium / Low = 0 / 1 / 2 = 2048 / 1024 /
// 512). The row raises only the High step: ext.max_shadow_size (0x1000 or
// 0x2000) raw at scalar 0, retail's 0x800 at every other step.
// ============================================================================
const bool* SunLight::_extMaxShadowRes = nullptr;
const int32_t* SunLight::_extMaxShadowSize = nullptr;

namespace {

constexpr int32_t kSunMapRetailSize = 0x800;    // the literal of the three Create calls
constexpr int32_t kSunMapMaxSize    = 0x1000;   // ext.max_shadow_size 4096 (the default)
constexpr int32_t kSunMapLargeSize  = 0x2000;   // ext.max_shadow_size 8192

// 8192 x 8192 D24S8 is 256 MB. The dimension always fits (the device is created at
// FL 10_0 at least: D3D10_REQ_TEXTURE2D_U_OR_V_DIMENSION = 8192, 16384 at FL 11_0),
// the memory may not. So the first time 8192 is asked for, a throwaway depth image
// of the map's exact descriptor is created through the engine's own ImageBuffer and
// deleted again; its Create result decides for the rest of the process. -1 unknown,
// 0 failed (4096 is used), 1 fits.
int g_largeSunMapFits = -1;

bool LargeSunMapFits(bool mayProbe) {
    if (g_largeSunMapFits < 0 && mayProbe && Video::Device::_singleton != nullptr &&
        Video::Device::_singleton->deviceData != nullptr) {
        Video::ImageBuffer* const probe = NewImageBuffer();
        const bool ok = probe != nullptr &&
                        probe->Create(0, 1, 7, 0x40, kSunMapLargeSize, kSunMapLargeSize, 1, 1, 1, 1, nullptr);
        DeleteImageBuffer(probe);
        g_largeSunMapFits = ok ? 1 : 0;
        if (!ok)
            std::fprintf(stderr, "[ShadowRes] an 8192x8192 sun map could not be created on this device: "
                                 "using 4096 (ext.max_shadow_size=8192 left as set)\n");
    }
    return g_largeSunMapFits != 0;   // unknown and not probing: the caller only compares
}

// The raw size the three Create sites pass. Faithful: one null test. `mayProbe`: the caller
// is a build or a main-thread request (never the per-frame call on the render thread).
int32_t SunMapRawSize(bool mayProbe) {
    const bool* const on = SunLight::_extMaxShadowRes;
    if (on == nullptr || !*on) return kSunMapRetailSize;
    if (Video::Configuration::_singleton->shadowScalar != 0) return kSunMapRetailSize;
    const int32_t* const size = SunLight::_extMaxShadowSize;
    if (size != nullptr && *size == kSunMapLargeSize && LargeSunMapFits(mayProbe)) return kSunMapLargeSize;
    return kSunMapMaxSize;
}

}  // namespace

namespace { const SunLight* g_extSunLight = nullptr; }   // the last light the sync saw (cleared in ReleaseFields)
#if BANISHED_TWEAK_OVERLAY
namespace { std::atomic<int32_t> g_tweakSunMapSize{0}; }   // the debug tweak overlay's stats (ExtTweakSunMapSize)
int32_t SunLight::ExtTweakSunMapSize() { return g_tweakSunMapSize.load(std::memory_order_relaxed); }
#endif

void SunLight::ExtRequestMaxShadowResSync() {
    // NOT RETAIL (review F-226): the faithful profile leaves the seam null, and then this does
    // nothing -- retail's OptionsDialog::ApplyVideoSettings (0x140071e10) writes no
    // Device::settingsChanged; its ApplyChangedSettings / Startup decide the rebuild.
    if (_extMaxShadowRes == nullptr) return;
    if (g_extSunLight != nullptr) g_extSunLight->ExtSyncMaxShadowRes(true);
    else if (Video::Device::_singleton != nullptr) Video::Device::_singleton->settingsChanged = 1;
}

void SunLight::ExtSyncMaxShadowRes(bool request) const {
    g_extSunLight = this;
    const int32_t want = SunMapRawSize(request);
    Video::Device* const dev = Video::Device::_singleton;
    if (_depthTarget->_width != want) {
        // The per-frame call (AddDrawItems, the render thread under ext.render_pipe) never
        // raises the rebuild: a mismatch waits for the next main-thread request
        // (the row toggle or the retail combo's apply, both call with request = true).
        if (!request) return;
        // Both descriptors; the companions are rebuilt from them by retail's
        // RecreateDynamic in the rebuild this flag requests (the frame that
        // raises it is refused by BeginCommands, as for a combo change).
        _depthTarget->_width = want;
        _depthTarget->_height = want;
        if (_colourTarget != nullptr) {
            _colourTarget->_width = want;
            _colourTarget->_height = want;
        }
        dev->settingsChanged = 1;
        return;
    }
    // One line per build of the map: its render buffer's companion is new after
    // every Create / RecreateDynamic, and the extent it carries was written
    // from the live texture's descriptor.
    if (dev->settingsChanged != 0) return;
    static const void* s_loggedCompanion = nullptr;
    static int32_t s_loggedWidth = 0;
    const int32_t built = _renderBuffer->width;
    if (_renderBuffer->platformData != s_loggedCompanion || built != s_loggedWidth) {
        s_loggedCompanion = _renderBuffer->platformData;
        s_loggedWidth = built;
#if BANISHED_TWEAK_OVERLAY
        g_tweakSunMapSize.store(built, std::memory_order_relaxed);
#endif
        std::fprintf(stderr,
                     "[ShadowRes] sun map %dx%d (raw %d, VideoShadowScalar=%d, ext.max_shadow_res=%d, "
                     "ext.max_shadow_size=%d, texelSize %.8f)\n",
                     built, _renderBuffer->height, _depthTarget->_width,
                     Video::Configuration::_singleton->shadowScalar, *_extMaxShadowRes ? 1 : 0,
                     _extMaxShadowSize != nullptr ? *_extMaxShadowSize : 0,
                     1.0f / static_cast<float>(_depthTarget->Width()));
    }
}

// ============================================================================
// Graphics::SunLight::Set(const Math::Vector3&, const Math::Vector4&,
//                         const Math::Vector4&)          retail 0x180107f10
//
// ELEVEN DWORD COPIES, then two rebuild branches this backend never takes. The
// copies are `MOV EAX,[src] / MOV [this+off],EAX` in the shipped body — no float
// arithmetic at all, so there is no association order to get wrong.
// ============================================================================
void SunLight::Set(const Math::Vector3& direction, const Math::Vector4& sun,
                   const Math::Vector4& ambient) {
    const float* const dir     = &direction.x;
    const float* const sunP    = &sun.x;
    const float* const ambientP = &ambient.x;

    // ---- the eleven copies, in the shipped order -------------------------
    _direction.x     = dir[0];
    _direction.y     = dir[1];
    _direction.z     = dir[2];
    _sun.x     = sunP[0];
    _sun.y     = sunP[1];
    _sun.z     = sunP[2];
    _sun.w     = sunP[3];
    _ambient.x = ambientP[0];
    _ambient.y = ambientP[1];
    _ambient.z = ambientP[2];
    _ambient.w = ambientP[3];

    // ---- the two rebuild branches ----------------------------------------
    if (DeviceSupportsColourShadowTarget() && _colourTarget == nullptr) {
        // The device grew the capability: build the colour target and a fresh
        // render buffer around it.
        DeleteRenderBuffer(_renderBuffer);
        _colourTarget = NewImageBuffer();
        // 0x800 in retail. ext.max_shadow_res: the depth's raw size, so the two
        // attachments always match (the depth is 0x800 unless the row is on).
        _colourTarget->Create(0, 0, 5, 0x40, _depthTarget->_width, _depthTarget->_width,
                              1, 1, 1, 1, nullptr);
        _renderBuffer = NewRenderBuffer();
        _renderBuffer->Create(_colourTarget, _depthTarget);
    } else if (!DeviceSupportsColourShadowTarget() && _colourTarget != nullptr) {
        // The device lost it: drop the colour target and rebuild depth-only.
        DeleteRenderBuffer(_renderBuffer);
        DeleteImageBuffer(_colourTarget);
        _colourTarget = nullptr;
        _renderBuffer = NewRenderBuffer();
        _renderBuffer->Create(_colourTarget, _depthTarget);
    }
    // A rebuild call verifies NOTHING against an oracle here, and the old
    // `float pre[11]` / `bool rebuilt` locals that said so were dead: both were
    // written and never read, and both were -Wunused warnings in the perf build.
    // The refusal is stated in the banner above the door instead.
}

}  // namespace Graphics

// ============================================================================
// ??0SunLight@Graphics@@QEAA@XZ 0x1801081f0 / ??1SunLight@Graphics@@UEAA@XZ
// 0x180108160 -- the two DOORS are gone (ENDGAME B.3.9 pilot, B39_RULES D1).
// Their one exe caller (MenuDialogLoopT_LoadSceneA1) builds
// `::new (p) Graphics::SunLight()`, whose compiler-emitted prologue is
// Geometry() and the Light / SunLight stamps; the only other reader of the
// ??1 door was the retired RT_DELDTOR row 0x180064620. The retail bodies
// live on unchanged in InitFields / ReleaseFields below, run by the real
// constructor and destructor (SunLight.h); ~Light and ~Geometry are the
// compiler's chain, retail's tail call 0x1801081db.
// ============================================================================

// ============================================================================
// Graphics::SunLight::InitFields / ReleaseFields (ENDGAME B.3.8 block RB,
// RULE B8-1): 0x1801081f0 after its two stamps, and 0x180108160 between its
// restamp and the ~Geometry tail. Shared by the real ctor/dtor (SunLight.h)
// and the two doors above. B.4: on the class's own fields (the POD view is
// gone).
// ============================================================================
void Graphics::SunLight::InitFields() {
    // +0xC0..+0xE8 (_direction, _sun, _ambient) are SunLight()'s
    // mem-initialisers (SunLight.h): retail's first stores after the stamps.
    // +0xEC is NOT written by retail and is NOT written here.
    _colourTarget = nullptr;
    _depthTarget  = nullptr;
    _renderBuffer = nullptr;
    _materialTypeFlags = 0x8000;

    // 0x800 in retail; ext.max_shadow_res may raise it (SunMapRawSize, above).
    const int32_t size = SunMapRawSize(true);
    if (DeviceSupportsColourShadowTarget()) {
        _colourTarget = NewImageBuffer();
        _colourTarget->Create(0, 0, 5, 0x40, size, size, 1, 1, 1, 1, nullptr);
    }

    // Unconditional — the join label 0x180108325 is BELOW the capability test's
    // JZ target, so the depth target and the render buffer are built whether or
    // not the colour target was.
    _depthTarget = NewImageBuffer();
    _depthTarget->Create(0, 1, 7, 0x40, size, size, 1, 1, 1, 1, nullptr);

    _renderBuffer = NewRenderBuffer();
    _renderBuffer->Create(_colourTarget, _depthTarget);

    // Inlined Geometry::SetBounds.
    ::Box& b = _localBounds;
    const bool same = b.min.x == kBoundsMin && b.min.y == kBoundsMin && b.min.z == kBoundsMin &&
                      b.max.x == kBoundsMax && b.max.y == kBoundsMax && b.max.z == kBoundsMax;
    if (!same) {
        b.min.x = kBoundsMin; b.min.y = kBoundsMin; b.min.z = kBoundsMin;
        b.max.x = kBoundsMax; b.max.y = kBoundsMax; b.max.z = kBoundsMax;
        _dirtyFlags |= 2u;
    }

}

void Graphics::SunLight::ReleaseFields() {
    if (Graphics::g_extSunLight == this) Graphics::g_extSunLight = nullptr;
    DeleteImageBuffer(_colourTarget);
    DeleteImageBuffer(_depthTarget);
    DeleteRenderBuffer(_renderBuffer);
}

