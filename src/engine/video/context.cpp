// ============================================================================
// src/engine/video/context.cpp — Wave 6 lane A — Video::Context
// ============================================================================
//
// Ports 23 of the 27 `Video::Context` exports our tree calls (PLAN.md §4.2
// wave 6, §6). The recovered layout and the evidence for every offset are in
// src/engine/video/Context.h; the per-function write-up, the oracle
// arguments and everything this lane could NOT verify are in
// docs/history/runtime_port/WAVE6_LANE_A.md.
//
//   | Export (short form)              | Retail      | Ord  | Ported | Oracle |
//   |----------------------------------|-------------|------|--------|--------|
//   | ??0Context (ctor)                | 0x1800f1820 |  125 | NO     | —      |
//   | ??1Context (dtor)                | 0x1800f16e0 |  562 | NO     | —      |
//   | ??3Context (operator delete)     | 0x180018500 |  945 | NO     | —      |
//   | ?AddVertex@                      | 0x18001ad40 | 1592 | yes    | replay |
//   | ?AddVertices@                    | 0x18001ad70 | 1593 | yes    | replay |
//   | ?BeginIndexedVertices@           | 0x1800f2170 | 1644 | yes    | thunk  |
//   | ?BeginInstancedIndexedVertices@  | 0x1800f2130 | 1645 | yes    | thunk  |
//   | ?BeginVertices@                  | 0x1800f21b0 | 1651 | yes    | thunk  |
//   | ?Clear@                          | 0x1800f23f0 | 1705 | yes    | thunk  |
//   | ?ClearResources@                 | 0x1800f2090 | 1712 | NO     | —      |
//   | ?DrawIndexed@                    | 0x1800f2410 | 2278 | yes    | thunk  |
//   | ?EndIndexedVertices@             | 0x1800f2150 | 2312 | yes    | thunk  |
//   | ?EndInstancedIndexedVertices@    | 0x1800f2110 | 2313 | yes    | thunk  |
//   | ?EndVertices@                    | 0x1800f2190 | 2318 | yes    | thunk  |
//   | ?GetMaxDynamicInstances@         | 0x1800f21d0 | 2774 | yes    | thunk  |
//   | ?Reset@                          | 0x1800f1e50 | 3573 | yes    | replay |
//   | ?SetCamera@                      | 0x18001a150 | 3837 | yes    | replay |
//   | ?SetColor@                       | 0x18001a8e0 | 3846 | yes    | replay |
//   | ?SetDefaultRenderBuffer@         | 0x1800ef3c0 | 3866 | yes    | replay |
//   | ?SetFog@                         | 0x18001a360 | 3884 | yes    | replay |
//   | ?SetImageBuffer@                 | 0x180019ed0 | 3893 | yes    | replay |
//   | ?SetInverseScale@                | 0x18001a880 | 3898 | yes    | replay |
//   | ?SetRenderBuffer@                | 0x1800f2100 | 3950 | yes    | replay |
//   | ?SetTime@                        | 0x18001a330 | 3988 | yes    | replay |
//   | ?SetTransform@                   | 0x18001a6b0 | 3991 | yes    | replay |
//   | ?SetVertexBuffer@                | 0x180019f20 | 4006 | yes    | replay |
//   | ?SetViewport@                    | 0x18001a060 | 4011 | yes    | replay |
//
// All 27 mangled names were checked against the shipped export directory of
// C:\GOG Games\Banished\Runtime-x64.dll with pefile BEFORE any body was
// written (reference_resolveexport_phantom_names.md: a name the DLL does not
// contain resolves to NULL in silence, the port still registers, and the
// census still counts it). All 27 are present; the addresses and ordinals in
// the table above are that query's output, not Ghidra's guess.
//
// ---------------------------------------------------------------------------
// WHY REGISTERING THIS PORT IS SAFE, EVEN THOUGH THE DLL KEEPS CALLING ITSELF
// ---------------------------------------------------------------------------
// Registering redirects OUR callers only. The DLL's own `Graphics::Renderer`,
// `UI::UserInterface` and `Engine::MainLoop` reach these same methods through
// direct near-CALLs the registry cannot see — verified, not assumed:
// get_xrefs_to on ?SetTransform@ names three internal callers
// (Graphics `Draw` 0x1800c38da, UI `Begin` 0x18011d389, `DrawItemList`
// 0x1801055f8), and ?Reset@ has one (`Engine::MainLoop::Draw` 0x1800dcd82).
// So for the rest of Wave 6 there really are two implementations operating on
// one live `Video::Context`, which is the hazard WAVE6.md warns about.
//
// It is safe here because of three specific facts, each of which is checked
// rather than assumed:
//
//   1. The class is NOT polymorphic. The ctor stamps no vtable (its first
//      store is the Device pointer) and no Context method is virtual, so there
//      is no vtable-ownership split (PLAN §4.3 risk 3 simply does not apply).
//   2. Nothing ported here allocates, frees, or changes the layout. Every
//      ported body reads and writes fields of an object somebody else owns,
//      at offsets `static_assert`ed in src/engine/video/Context.h.
//   3. The replay oracle below proves, on every diffed call, that our body
//      leaves the WHOLE 0x2168-byte object byte-identical to what the DLL's
//      body would have left. That is what makes "both implementations on one
//      object" a non-event: after either one runs, the object is in the same
//      state, so the next call — ours or theirs — sees exactly what it expects.
//
// NO DETOUR IS INSTALLED, deliberately. A detour (PLAN §4.1 decision 3) would
// route the DLL's internal render-loop callers into this port too. That is a
// much larger blast radius on the hottest path in the process, and it buys
// nothing the argument above does not already give us: the DLL's callers are
// happy with the DLL's bodies, and our callers are proven equivalent. When
// `Graphics::Renderer` itself is ported (lane F and later waves) those edges
// become ours by ordinary registration and the question disappears.
//
// ---------------------------------------------------------------------------
// WHAT STAYS IN THE DLL, AND WHY
// ---------------------------------------------------------------------------
// THREE FUNCTIONS ARE HELD BACK. All three are lifecycle, all three would
// split ownership of one object between two implementations, and all three
// have ZERO live callers in our tree today.
//
//   ??0Context@Video@@QEAA@PEAVDevice@1@HHHPEAVRenderBuffer@1@@Z  (ctor)
//       The object is created and destroyed entirely inside the DLL:
//       `Engine::MainLoop::CreateVideoContext` (0x1800dd500) allocates 0x2168
//       bytes from `System::MemoryAllocator` and runs this ctor, and
//       `CreateVideoContext` / `ReleaseVideoContext` / `~MainLoop` /
//       `OnVideoError` run the dtor. (This used to say "`Engine::MainLoop` is
//       Wave 8, not Wave 6", which now reads as resolved -- Wave 8 is closed.
//       It is not a wave-ownership question: `engine.cpp:367` calls the DLL's
//       `CreateVideoContext` BY ADDRESS, which is a call-through to the DLL's
//       own ctor, not a reimplementation. Corrected in the Wave 8.5 close-out;
//       the `fn_VideoCtx_Ctor` this sentence used to name reached zero callers
//       and was DELETED in ENDGAME B.3.5, batch b35-K2 (41baab1c).)
//       The ctor also allocates FIVE sub-objects (a VertexBuffer, an
//       IndexBuffer, an instance VertexBuffer, a RenderBuffer and two
//       ImageBuffers) through each class's own `operator new`, and calls each
//       one's `PlatformConstruct`. Porting it would mean our code allocates
//       objects the DLL's dtor later frees, on a path we never take.
//       Our tree has NO reference to it at all any more. It used to have one,
//       `fn_VideoCtx_Ctor` in engine/funcs/engine_funcs.cpp, which was resolved
//       and never called; ENDGAME B.3.5 batch b35-K2 deleted that pointer with
//       the rest of the Context lifecycle group.
//
//   ??1Context@Video@@QEAA@XZ                                     (dtor)
//       The mirror image: it frees those five sub-objects and destroys seven
//       embedded `ConstantBuffer`s. Same ownership split, same zero callers --
//       the `fn_VideoCtx_Dtor` that was resolved and never called is itself gone
//       now (ENDGAME B.3.5, b35-K2).
//
//   ??3Context@Video@@SAXPEAX@Z                            (operator delete)
//       Three lines, and every one of them is out of scope. It takes the
//       allocator's critical section (`_singleton + 0x640`) and calls
//       `System::MemoryAllocator::Free`. `EnterCriticalSection` is Win32, and
//       PLAN §4.1 decision 1 keeps src/engine/ platform-neutral outside a
//       `_win` twin; `MemFree` itself is Wave 2's allocator port, which
//       shipped OPT-IN with a NO-GO verdict (WAVE2_ALLOCATOR.md). A Context
//       port has no business being the thing that finally arms it.
//
// A FOURTH IS HELD BACK FOR A DIFFERENT REASON.
//
//   ?ClearResources@Context@Video@@QEAAXXZ
//       It is pure, self-contained and trivially portable — and it has no
//       caller. `fn_VideoCtx_ClearResources` used to be resolved-and-never-invoked
//       in engine_funcs.cpp; it was deleted for that reason in ENDGAME B.3.5
//       (b35-K2), and no DLL-internal caller exists either.
//       A registered port on a path nothing executes reports `calls=0` and
//       `divergences=0` forever, which reads as a pass and proves nothing;
//       that is exactly the trap reference_uncalled_port_is_not_verified.md
//       records (a wrong `Quaternion::operator*` survived all of Wave 1 that
//       way). An honest hold-back beats an unverifiable port. The body is
//       fully reversed and written up in WAVE6_LANE_A.md, so landing it costs
//       nothing the day something calls it.
//
// The other ~90 exported Context methods (?SetLight@, ?SetBones@,
// ?BeginCommands@, the private ?Bind*@ family, …) are not in this lane's scope
// because our tree never calls them; they stay in the DLL untouched.
//
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "math/Math.h"
#include "video/Context.h"
// ENDGAME B.3.5 block J: the real Video::ContextData, block A's abstract base,
// whose twenty slots the nine backend forwarders below now dispatch through.
#include "video/CompanionData.h"

// ENDGAME B.3.5 block D1. `Video::Context` and `Video::Viewport` are real
// classes now (src/engine/video/Context.h): every body below MOVED onto the
// member, `self->x` became `x`, and each registered door kept its name, its
// mangled-name row and its address row and became a one-line QUALIFIED
// non-virtual delegation. Nothing is unregistered and nothing is retired
// (B35_RULES 11).
//
// THE THREE B.3.1 BRIDGES IN THIS FILE ARE GONE WITH THE CONVERSION. `MatrixRaw`
// and `Vector4Raw` were byte-identical stand-ins for `Math::Matrix` and
// `Math::Vector4` that existed only because `Video::Context` was a POD; the
// Context now holds the real classes by value (both are trivially copyable, as
// Context.h asserts), so `MatrixInverse`, `MatrixMul`, `MatrixNotEqual`, their
// three `reinterpret_cast`s and the `sizeof(MatrixRaw) == sizeof(Math::Matrix)`
// bridge assert are all deleted rather than moved. `Matrix::Inverse`,
// `operator*` and `operator!=` are called as members, which is what B.3.1 step 2
// left them ready for.
using Video::Context;
using Video::Viewport;

namespace {

// ENDGAME B.3.5 block J — `BackendSlot<Fn>(this, byteOffset)` IS GONE. It read
// the vptr at `_contextData+0x00` by hand and indexed it at `byteOffset / 8`;
// `Video::ContextData` (block A, src/engine/video/CompanionData.h) declares all
// twenty slots in retail order, so a VIRTUAL CALL through it reads THE SAME live
// vptr at THE SAME byte offset and is the same dispatch, with the index checked by
// the compiler instead of by a comment. The `kSlot*` byte-offset enum in Context.h
// stays as the documentation of what each index is, and the nine asserts below pin
// each one to its slot so the mapping is checked rather than stated in prose.
//
// The object behind the pointer is a `VideoDX11::ContextData` wearing dx11's
// vtable; `_contextData` is typed `Video::ContextData*` (B.4 R-VIDEO), so the
// receiver needs no view.
//
// NO NULL CHECK ON _contextData — retail has none (`MOV RCX,[RCX+0x2160]` then
// `MOV RAX,[RCX]` unguarded), and CLAUDE.md's rule applies: a guard here would
// not restore the behaviour, it would skip it. If this ever faults, the
// ContextData was never created and that is the bug to find. The virtual call is
// the same unguarded double deref.
inline Video::ContextData* Backend(const Context* self) {
    return self->_contextData;
}

// The nine byte offsets this file dispatched through, each pinned to the slot
// index `Video::ContextData` declares it at. A slot that moved in the header would
// break the build here instead of silently calling its neighbour.
static_assert(RtVideo::kSlotClear                         / 8 ==  4, "ContextData::Clear is slot 4");
static_assert(RtVideo::kSlotDrawIndexed                   / 8 == 12, "ContextData::DrawIndexed is slot 12");
static_assert(RtVideo::kSlotGetMaxDynamicInstances        / 8 == 13, "ContextData::GetMaxDynamicInstances is slot 13");
static_assert(RtVideo::kSlotBeginVertices                 / 8 == 14, "ContextData::BeginVertices is slot 14");
static_assert(RtVideo::kSlotEndVertices                   / 8 == 15, "ContextData::EndVertices is slot 15");
static_assert(RtVideo::kSlotBeginIndexedVertices          / 8 == 16, "ContextData::BeginIndexedVertices is slot 16");
static_assert(RtVideo::kSlotEndIndexedVertices            / 8 == 17, "ContextData::EndIndexedVertices is slot 17");
static_assert(RtVideo::kSlotBeginInstancedIndexedVertices / 8 == 18, "ContextData::BeginInstancedIndexedVertices is slot 18");
static_assert(RtVideo::kSlotEndInstancedIndexedVertices   / 8 == 19, "ContextData::EndInstancedIndexedVertices is slot 19");

// The hidden-return-slot note this file used to carry about
// `Math::Matrix::Inverse` and `Math::Matrix::operator*` is settled and no longer
// applies: both are real members of a real class compiled by this compiler
// (src/engine/math/Math.h), so the by-value return is just the C++ return and
// the RDX/RAX shape is the compiler's problem, not the port's. The retail
// disassembly of SetCamera at 0x18001a165-0x18001a170 (`LEA RDX,[RSP+0x20] /
// MOV RCX,RDI / CALL 0x1800bb310`) is what proved the argument ORDER —
// `_inverseView = view.Inverse()` and then `_viewProjection = _inverseView *
// projection`, with the FIELD as the left operand — and that order is preserved
// below.

} // namespace

// ============================================================================
// THE MEMBERS — ENDGAME B.3.5 block D1
// ============================================================================
// Every body is byte-faithful to the retail decompilation. Where retail writes
// a field it does not compare first, so do we; where retail compares before
// writing (so the dirty bit is only set on a real change), so do we — that
// early-out is observable state, not an optimisation.
//
// The bodies are UNCHANGED by the conversion except that `self->x` became `x`
// and the three bridge helpers became direct member calls on
// `Math::Matrix`. Each one's registered door is below, under its old name.
namespace Video {

// ---- ?SetColor@Context@Video@@QEAAXAEBVVector4@Math@@@Z  0x18001a8e0 -------
void Context::SetColor(const Math::Vector4& color) {

    if (_color.x != color.x || _color.y != color.y ||
        _color.z != color.z || _color.w != color.w) {
        _color = color;
        _dirty |= RtVideo::kDirtyInstanceConstants;
    }

}

// ---- ?SetInverseScale@Context@Video@@QEAAXMMMM@Z  0x18001a880 --------------
void Context::SetInverseScale(float x, float y, float z, float w) {

    if (x != _inverseScale.x || y != _inverseScale.y ||
        z != _inverseScale.z || w != _inverseScale.w) {
        _inverseScale.x = x;
        _inverseScale.y = y;
        _inverseScale.z = z;
        _inverseScale.w = w;
        _dirty |= RtVideo::kDirtyInstanceConstants;
    }

}

// ---- ?SetTime@Context@Video@@QEAAXM@Z  0x18001a330 ------------------------
// Note there is no compare-first here: retail writes the four floats and ORs
// the dirty bit unconditionally, and it shares bit 0x800 with SetCamera.
void Context::SetTime(float time) {

    _time.x = time;
    _time.y = 0.0f;
    _time.z = 0.0f;
    _time.w = 0.0f;
    _dirty |= RtVideo::kDirtyGlobalConstants;

}

// ---- ?SetFog@Context@Video@@QEAAXMMAEBVVector4@Math@@@Z  0x18001a360 ------
// _fogParams = { -nearZ, 1 / (farZ - nearZ), 0, 0 }, and the colour is stored
// TWICE: once at +0x3e8 and once at +0x258, inside the pixel-constant staging
// block. Both stores are retail's; the second is not a typo in the decompile.
//
// The negation is a SIGN-BIT XOR (`XORPS XMM3,[0x1801617c0]`, a 16-byte
// 0x80000000 quad), not `0 - nearZ`. Reproduced bitwise so that a fog plane at
// exactly 0.0f yields -0.0f here, the same as retail.
void Context::SetFog(float nearZ, float farZ, const Math::Vector4& color) {

    uint32_t nearBits;
    std::memcpy(&nearBits, &nearZ, 4);
    nearBits ^= 0x80000000u;
    float negNear;
    std::memcpy(&negNear, &nearBits, 4);

    const float invRange = 1.0f / (farZ - nearZ);

    _fogParams.x = negNear;
    _fogParams.y = invRange;
    _fogParams.z = 0.0f;
    _fogParams.w = 0.0f;
    _fogColor = color;

    // The +0x258 duplicate, inside the pixel-constant staging block.
    std::memcpy(&_pixelConstantStaging.pixel[1], &color, sizeof(Math::Vector4));   // +0x258

    _dirty |= (RtVideo::kDirtyGlobalConstants | RtVideo::kDirtyPixelConstants);

}

// ---- ?SetViewport@Context@Video@@QEAAXAEBVViewport@2@@Z  0x18001a060 ------
// The comparison is OPEN-CODED, not a call to `??8Viewport@Video@@`: retail
// compares the four dwords in line here (0x18001a060) and emits the operator as
// a separate body at 0x180018240 that this function does not call. The two are
// the same four compares; keeping them apart keeps the transcription honest.
void Context::SetViewport(const Viewport& vp) {

    if (_viewport.x != vp.x || _viewport.y != vp.y ||
        _viewport.width != vp.width || _viewport.height != vp.height) {
        // Retail sets the dirty bit BEFORE copying; the order is not
        // observable from outside but is kept for byte-faithfulness.
        _dirty |= RtVideo::kDirtyViewport;
        _viewport = vp;
    }

}

// ---- ?SetImageBuffer@Context@Video@@QEAAXPEBVImageBuffer@2@H@Z  0x180019ed0 -
// `1 << (slot & 0x1f)` is retail's own masking; with 16 slots it never wraps,
// but the AND is in the shipped code and is kept.
void Context::SetImageBuffer(const ImageBuffer* image, int32_t slot) {

    if (image != _imageBuffers[slot]) {
        _dirty |= RtVideo::kDirtyImageBuffers;
        _imageBufferDirty |= 1u << (static_cast<unsigned>(slot) & 0x1fu);
        _imageBuffers[slot] = image;
    }

}

// ---- ?SetVertexBuffer@Context@Video@@QEAAXPEBVVertexBuffer@2@HHH@Z  0x180019f20
// B.4 (R-VIDEO): MOVED to context_bind.cpp, beside BindResources, its consumer.

// ---- ?SetRenderBuffer@Context@Video@@QEAAXPEBVRenderBuffer@2@@Z  0x1800f2100
void Context::SetRenderBuffer(const RenderBuffer* rb) {

    if (_renderBuffer != rb) {
        _dirty |= RtVideo::kDirtyRenderBuffer;
        _renderBuffer = rb;
    }

}

// ---- ?SetDefaultRenderBuffer@Context@Video@@QEAAXPEAVRenderBuffer@2@@Z  0x1800ef3c0
// B.4 (R-VIDEO): MOVED to context_ctor.cpp, beside the ctor that first sets it.

// ---- ?SetTransform@Context@Video@@QEAAXHAEBVMatrix@Math@@@Z  0x18001a6b0 ---
// The transform-count update is OUTSIDE the change test: retail raises the
// high-water mark even when the matrix is unchanged. That asymmetry is the
// kind of thing a "tidied" port silently drops.
//
// `operator!=` is `??9Matrix@Math@@QEBA_NAEBV01@@Z` (0x18001d850), a real
// exported member of a real class since B.3.1 step 2 — the call at 0x18001a6d5
// is exactly this one.
void Context::SetTransform(int32_t slot, const Math::Matrix& m) {

    Math::Matrix& dst = _transforms[slot];
    if (m != dst) {
        dst = m;
        _dirty |= RtVideo::kDirtyTransformConstants;
    }
    if (_transformCount < slot + 1) {
        _transformCount = slot + 1;
    }

}

// ---- ?SetBones@Context@Video@@QEAAXHPEBVMatrix@Math@@@Z  0x18001a780 -------
// ---- ordinal 3835.  BATCH 8. ------------------------------------------------
// Writes `count` to `_boneCount` (the same "live counter" BindResources'
// transform-dirty test already reads alongside `_transformCount`), then
// copies `count` Matrix entries (0x40 bytes each) from `matrices` into
// `_boneAndProjectionStaging` starting at its own +0x00 — i.e. bone i lands
// at that array's byte offset `i * 0x40` — ORing the transform-constants
// dirty bit into `_dirty` on every iteration (retail re-ORs it once per bone,
// not once at the end; reproduced faithfully even though it is redundant
// after the first iteration, since retail's own listing does exactly this).
// A `count <= 0` call touches only `_boneCount` and nothing else, matching
// retail's `if (0 < param_1)` guard around the whole copy loop.
void Context::SetBones(int32_t count, const Math::Matrix* matrices) {

    _boneCount = count;
    for (int32_t i = 0; i < count; ++i) {
        std::memcpy(_boneAndProjectionStaging + static_cast<size_t>(i) * sizeof(Math::Matrix),
                    &matrices[i], sizeof(Math::Matrix));
        _dirty |= RtVideo::kDirtyTransformConstants;
    }

}

// ---- ?SetTransformScale@Context@Video@@QEAAXHAEBVMatrix@Math@@AEBVVector4@4@@Z
// ---- 0x18001a400, ordinal 3994.  WAVE 28 LANE MISC. ----------------------
//
// The name is a trap and the disassembly says so: there is NOT ONE `mulss` in
// the whole body.  Nothing is scaled BY anything.  What retail does is build a
// copy of `m` in which the FOURTH COLUMN (m[3], m[7], m[11], m[15] -- byte
// offsets 0x0c/0x1c/0x2c/0x3c) is REPLACED wholesale by the four components of
// `scale`, and then run the identical compare-store-dirty-highwater tail as
// SetTransform above.
//
// This is exactly kickoff warning 3's shape ("count the STORES, not the
// fields"), and it is the same w-column trap MeshInstance::GetInstanceTransform
// set for Wave 28.  The obvious port -- multiply the matrix by the scale
// vector, or scale the three basis rows -- passes every "it is a scale
// function" reading and writes different bytes.  What pins it is the STORE
// COUNT: sixteen `movss` into `rbx`, twelve sourced from `r8` (the matrix) and
// FOUR sourced from `r9` (the vector), at precisely the four w-column offsets.
// Traced store by store from objdump at 0x18001a5c4..0x18001a647 against the
// scratch built at 0x18001a4c9..0x18001a5a7; both halves agree.
//
// Everything after the build is SetTransform's tail verbatim:
//   * the compare is `??9Matrix@Math@@QEBA_NAEBV01@@Z` (0x18001d850, the call
//     at 0x18001a5b7) -- store only on a real change,
//   * the dirty bit is 0x4000 (`or [rdi+0x10],0x4000`) = kDirtyTransformConstants,
//   * and the high-water update of `_transformCount` is OUTSIDE the change
//     test (the `je` at 0x18001a5be lands ON the cmovg at 0x18001a64e), so the
//     count rises even when the matrix is unchanged.  Same asymmetry, same
//     reason it must not be tidied.
void Context::SetTransformScale(int32_t slot, const Math::Matrix& m,
                               const Math::Vector4& scale) {

    Math::Matrix built;
    for (int i = 0; i < 16; ++i) built.m[i] = m.m[i];
    built.m[3]  = scale.x;
    built.m[7]  = scale.y;
    built.m[11] = scale.z;
    built.m[15] = scale.w;

    Math::Matrix& dst = _transforms[slot];
    if (built != dst) {
        dst = built;
        _dirty |= RtVideo::kDirtyTransformConstants;
    }
    if (_transformCount < slot + 1) {
        _transformCount = slot + 1;
    }

}

// ---- ?SetCamera@Context@Video@@QEAAXAEBVMatrix@Math@@0@Z  0x18001a150 -----
// _inverseView   = view.Inverse()
// _viewProjection = _inverseView * projection      (in that order — the left
//                                                   operand is the FIELD that
//                                                   was just written, not the
//                                                   local result)
// _cameraPosition = view row 3
void Context::SetCamera(const Math::Matrix& view, const Math::Matrix& projection) {

    _inverseView    = view.Inverse();
    _viewProjection = _inverseView * projection;

    _cameraPosition.x = view.m[12];
    _cameraPosition.y = view.m[13];
    _cameraPosition.z = view.m[14];
    _cameraPosition.w = view.m[15];

    _dirty |= RtVideo::kDirtyGlobalConstants;

}

// ---- ?Reset@Context@Video@@QEAAXXZ  0x1800f1e50 ---------------------------
// The frame reset: clear every binding, mark everything dirty, and re-arm the
// viewport and scissor to the default render buffer's dimensions.
//
// Two details a readable rewrite would get wrong and the replay oracle would
// catch: (1) `_dirty` is ASSIGNED 0x1fe00 and then OR'd bit by bit as each
// binding is found non-null, so a Reset on an already-clean Context leaves a
// different value from one on a dirty Context; (2) the vertex-slot test also
// fires when only the offset or stride is non-zero, while the image-slot test
// looks at the pointer alone.
void Context::Reset() {

    _dirty = RtVideo::kDirtyAllConstants;

    // Retail's three leading memsets. Their boundaries do NOT line up with
    // named fields: the first two start at +0x208 and +0x248 — eight bytes
    // past _pixelProgram, inside the pixel-constant staging block (+0x200) —
    // and stop short of _materialConstants at +0x298, which Reset does not
    // clear; the third covers +0x328..+0x3f7, _viewProjection through _fogColor.
    // B.4 (R-VIDEO): each is spelled from the member it starts in.
    std::memset(_pixelConstantStaging.material, 0, 0x40);   // +0x208
    std::memset(static_cast<void*>(_pixelConstantStaging.pixel), 0, 0x50);      // +0x248 (Vector4 rows)
    static_assert(offsetof(Context, _inverseScale) - offsetof(Context, _viewProjection) == 0xd0,
                  "Reset's third memset ends where _inverseScale begins");
    std::memset(static_cast<void*>(&_viewProjection), 0, 0xd0);
    _inverseScale = Math::Vector4();
    _color        = Math::Vector4();
    std::memset(_boneAndProjectionStaging, 0, sizeof(_boneAndProjectionStaging));
    // `static_cast<void*>` here is not decoration: `Math::Matrix` has a retail
    // default constructor that zeroes all sixteen cells (Math.h:218), so it is
    // not trivially default-constructible and `-Wclass-memaccess` objects to a
    // raw `memset` over an array of them. Retail does ONE memset; a
    // value-initialising loop would write the same 0xd00 bytes but would stop
    // being the single memset the disassembly shows. The cast says "yes, bytes"
    // and keeps the transcription.
    std::memset(static_cast<void*>(_transforms), 0, sizeof(_transforms));
    std::memset(static_cast<void*>(&_lightStaging), 0, sizeof(_lightStaging));

    _transformCount = 0;
    _boneCount    = 0;   // retail zeroes +0x20 as one qword

    for (int i = 0; i < RtVideo::kImageBufferSlots; ++i) {
        if (_imageBuffers[i] != nullptr) {
            _dirty |= RtVideo::kDirtyImageBuffers;
            _imageBufferDirty |= 1u << i;
            _imageBuffers[i] = nullptr;
        }
    }
    for (int i = 0; i < RtVideo::kVertexBufferSlots; ++i) {
        if (_vertexBuffers[i] != nullptr || _vertexOffsets[i] != 0 ||
            _vertexStrides[i] != 0) {
            _dirty |= RtVideo::kDirtyVertexBuffers;
            _vertexBufferDirty |= 1u << i;
            _vertexBuffers[i] = nullptr;
            _vertexOffsets[i] = 0;
            _vertexStrides[i] = 0;
        }
    }
    if (_indexBuffer != nullptr || _indexOffset != 0) {
        _dirty |= RtVideo::kDirtyIndexBuffer;
        _indexBuffer = nullptr;
        _indexOffset = 0;
    }
    if (_renderBuffer != nullptr) {
        _dirty |= RtVideo::kDirtyRenderBuffer;
        _renderBuffer = nullptr;
    }
    if (_vertexLayout != nullptr) {
        _dirty |= RtVideo::kDirtyVertexLayout;
        _vertexLayout = nullptr;
    }
    if (_vertexProgram != nullptr) {
        _dirty |= RtVideo::kDirtyVertexProgram;
        _vertexProgram = nullptr;
    }
    if (_pixelProgram != nullptr) {
        _dirty |= RtVideo::kDirtyPixelProgram;
        _pixelProgram = nullptr;
    }

    // Unguarded deref of _defaultRenderBuffer, exactly as retail: it is set by
    // the ctor (or by SetDefaultRenderBuffer) and Reset relies on it being
    // there. CLAUDE.md: do not "fix" this with a null check.
    const RenderBuffer* rb = _defaultRenderBuffer;
    const int32_t w = rb->width;
    const int32_t h = rb->height;

    if (_viewport.x != 0 || _viewport.y != 0 ||
        _viewport.width != w || _viewport.height != h) {
        _dirty |= RtVideo::kDirtyViewport;
        _viewport.x = 0;
        _viewport.y = 0;
        _viewport.width  = w;
        _viewport.height = h;
    }
    // Retail re-reads the two dimensions from the render buffer here rather
    // than reusing the values above. Kept.
    const int32_t w2 = rb->width;
    const int32_t h2 = rb->height;
    if (_scissor.x != 0 || _scissor.y != 0 ||
        _scissor.width != w2 || _scissor.height != h2) {
        _dirty |= RtVideo::kDirtyScissor;
        _scissor.x = 0;
        _scissor.y = 0;
        _scissor.width  = w2;
        _scissor.height = h2;
    }
    if (_renderState != nullptr) {
        _dirty |= RtVideo::kDirtyRenderState;
        _renderState = nullptr;
    }

}

// ---- ?AddVertex@Context@Video@@QEAAXPEBX@Z  0x18001ad40 -------------------
// Copies one vertex of `_dynamicVertexStride` bytes to the mapped write cursor
// and advances it. No bounds check — the caller promised a count to
// BeginVertices and retail trusts it.
//
// REPLAY NOTE. The DLL's body writes through the cursor too, into the SAME
// mapped buffer at the SAME address with the SAME source bytes (the scratch
// Context is a copy, so its cursor is the value ours started from). The second
// write is byte-for-byte identical to the first, so replaying it is idempotent
// rather than a second, different write into live state. That is the one place
// in this lane where the original touches something outside the scratch copy,
// and it is deliberate: the alternative is no oracle at all.
void Context::AddVertex(const void* vertex) {

    const size_t stride = static_cast<size_t>(_dynamicVertexStride);
    std::memcpy(_dynamicVertexCursor, vertex, stride);
    _dynamicVertexCursor += stride;

}

// ---- ?AddVertices@Context@Video@@QEAAXHPEBX@Z  0x18001ad70 ----------------
// The byte count is `count * stride` computed in 32-bit INT and then widened,
// which is what retail does (`IMUL` on the 32-bit registers before the
// sign-extending move). A count large enough to overflow would wrap the same
// way in both. Reproduced rather than promoted to 64-bit.
void Context::AddVertices(int32_t count, const void* vertices) {

    const int32_t bytes = count * _dynamicVertexStride;
    std::memcpy(_dynamicVertexCursor, vertices, static_cast<size_t>(bytes));
    _dynamicVertexCursor += bytes;

}

// ============================================================================
// The nine backend forwarders
//
// Each is a one-line tail call through the ContextData vtable at the byte
// offset named in src/engine/video/Context.h. Their oracle is VerifyThunk,
// run once at registration; there is no per-call comparison because running
// the original as well would submit the draw twice.
//
// ENDGAME B.3.5 block J, COORDINATOR RULING: THESE NINE ARE CONVERTED. Block D1
// left them hand-inlined, on the reading that `_contextData` points at a
// `VideoDX11::ContextData` rather than a `Video::ContextData` because the concrete
// side does not derive in C++ (RULE B5-8's declared leave). That is true and it is
// not an obstacle: a virtual call through the abstract `Video::ContextData*` reads
// the LIVE vptr at the same byte offset and indexes the same array, so it IS the
// same dispatch — the seam is still what fills the slots, and it still fills all
// twenty. What changes is that the slot INDEX is now checked by the compiler and
// the argument list by the declaration, instead of by a `using Fn = ...` typedef
// nobody could diff against the header. Slots 17 and 19 remain two names for one
// dx11 body: dx11 declares slot 17 as a forwarder to slot 19.
//
// THE SLOT DECIDES, and each index was checked against `Video::ContextData`'s
// declaration order and against dx11's twenty slots (B.3.5: the
// `DX11_REGISTER_SLOT` rows; since B5 the `override` declarations in
// src/render/dx11/VideoDX11Data.h); the nine `static_assert`s above pin the byte offsets.
// ============================================================================

void Context::Clear(ClearFlags flags, uint32_t color, float depth, int32_t stencil) {
    Backend(this)->Clear(flags, color, depth, stencil);
}

void Context::DrawIndexed(int32_t primitiveType, int32_t a, int32_t b,
                          int32_t c, int32_t d, int32_t e) {
    Backend(this)->DrawIndexed(primitiveType, a, b, c, d, e);
}

int32_t Context::GetMaxDynamicInstances(int32_t a, int32_t b, int32_t c) const {
    return Backend(this)->GetMaxDynamicInstances(a, b, c);
}

bool Context::BeginVertices(int32_t count, int32_t stride) {
    return Backend(this)->BeginVertices(count, stride);
}

void Context::EndVertices(int32_t primitiveType) {
    Backend(this)->EndVertices(primitiveType);
}

bool Context::BeginIndexedVertices(int32_t a, int32_t b, int32_t c) {
    return Backend(this)->BeginIndexedVertices(a, b, c);
}

void Context::EndIndexedVertices(int32_t primitiveType) {
    Backend(this)->EndIndexedVertices(primitiveType);
}

bool Context::BeginInstancedIndexedVertices(int32_t a, int32_t b, int32_t c, int32_t d) {
    return Backend(this)->BeginInstancedIndexedVertices(a, b, c, d);
}

void Context::EndInstancedIndexedVertices(int32_t primitiveType) {
    Backend(this)->EndInstancedIndexedVertices(primitiveType);
}

// ============================================================================
// Video::Viewport's two comparison operators
// ============================================================================
// `??8Viewport@Video@@QEBA_NAEBV01@@Z` 0x180018240 and
// `??9Viewport@Video@@QEBA_NAEBV01@@Z` 0x180018270, both read as disassembly
// this pass. Retail emits TWO separate bodies rather than one plus a negation,
// and each compares the four dwords in x / y / width / height order with a
// short-circuit exit, so they are written that way: `??9` is not `!(*this ==
// other)`. The four-dword shape is also what pins the field order, together
// with the four-argument constructor at 0x180018220.
//
// These are the only two `Video::Viewport` exports this block ports. The two
// constructors and `operator=` stay DOORS — see the triviality banner on the
// class in Context.h: `Video::Context` holds two Viewports by value and must
// stay trivially copyable and trivially default-constructible, and retail's own
// default constructor ZEROES, so neither a user-provided body nor `= default`
// is available here.
bool Viewport::operator==(const Viewport& other) const {
    if (x != other.x) return false;
    if (y != other.y) return false;
    if (width != other.width) return false;
    if (height != other.height) return false;
    return true;
}

bool Viewport::operator!=(const Viewport& other) const {
    if (x != other.x) return true;
    if (y != other.y) return true;
    if (width != other.width) return true;
    if (height != other.height) return true;
    return false;
}

}  // namespace Video

// ============================================================================
// STALE COMMENTS THIS LANE DISPROVED (PLAN §6 step 8)
// ============================================================================
// Lane A may only edit its own three files, so the corrections below are
// recorded here and in docs/history/runtime_port/WAVE6_LANE_A.md for the wave lead to
// apply at their source.
//
//  1. docs/history/runtime_port/wave6_lanes.txt lists all 27 lane-A rows with
//     `kind = export`. THREE OF THEM ARE RESOLVED BY ADDRESS, not by name:
//     engine/funcs/engine_funcs.cpp:1620 resolves the ctor with
//     `ResolveAddr(Module::Runtime, 0x1800f1820)` and :1628 resolves
//     `operator delete` with `ResolveAddr(Module::Runtime, 0x180018500)`;
//     the dtor at :1622 tries the name first and falls back to
//     `ResolveAddr(Module::Runtime, 0x1800f16e0)`. A name-only registration of
//     those three would have registered nothing anybody calls — which happens
//     to be harmless here only because all three are held back.
//
//  2. RESOLVED AND CLOSED. This lane reported `fn_VideoCtx_Ctor`,
//     `fn_VideoCtx_Dtor`, `fn_VideoCtx_Delete` and `fn_VideoCtx_ClearResources`
//     as DEAD RESOLVES -- assigned in `EngineFuncsInit`, mentioned nowhere else
//     but a diagnostic printf, called by nothing in code/, engine/, standalone/,
//     runtime/ or src/render/dx11/ -- and noted that `fn_VideoCtx_Reset` was the only
//     member of the group with a live caller, in src/app/main.cpp.
//
//     ALL FIVE ARE GONE. ENDGAME B.3.5 batch b35-I3 moved main.cpp's
//     FSE-refocus recovery onto `Video::Context::Reset()`, which emptied the
//     last of them, and batch b35-K2 (41baab1c) deleted the whole group --
//     typedefs, externs, definitions, initialisers and dump lines. So the
//     `// resolved, never called` line this note asked for is moot: there is no
//     pointer left to put it on. THE REGISTERED DOORS AND THE HELD-BACK
//     CLASSIFICATION BELOW ARE UNAFFECTED -- what died was engine_funcs's copy
//     of the addresses, not the ports or the registrations.
// ============================================================================
