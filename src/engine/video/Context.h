#pragma once
// ============================================================================
// src/engine/video/Context.h — Video::Context and Video::Viewport, the real
// classes. ENDGAME B.3.5 block D1 (RULE B5-4, B35_RULES 2.4 / 3 / 10 move 7).
// ============================================================================
//
// `Video::Context` is the drawing state machine: a big, flat, NON-POLYMORPHIC
// state block that the game fills in with setters and then hands to the backend
// (`Video::ContextData*` at +0x2160, which today is OUR code —
// src/render/dx11/contextdata.cpp).
//
// WHAT B.3.5 CHANGED, AND WHAT IT DELIBERATELY DID NOT
// ---------------------------------------------------------------------------
// The layout below is Wave 6 lane A's, byte for byte, with three type changes
// and no offset changes:
//
//   * `RtVideo::MatrixRaw`  -> `Math::Matrix`   (retired)
//   * `RtVideo::Vector4Raw` -> `Math::Vector4`  (ditto)
//   * `RtVideo::ConstantBufferOpaque` (0x48 opaque bytes) -> the real
//     `Video::ConstantBuffer` (src/engine/video/ConstantBuffer.h), whose
//     interior B.3.5 block D1 closed out of the retail accessors — that was
//     B35_RULES section 13 item 4's gap.
//
// All three are layout-identical AND trivially copyable, which is the condition
// the block brief set for making the swap: `sizeof`/`alignof`/`offsetof` are
// asserted below exactly as before, and `std::is_trivially_copyable<Context>`
// is asserted for the first time so that the tree's `memcpy` / `memset` /
// `reinterpret_cast` traffic over these 0x2168 bytes stays well defined.
// `Math::Matrix` and `Math::Vector4` are NOT trivially default-constructible
// (their retail default constructors zero the storage, Math.h:218/175), so
// `Context` is not either — which is fine and is asserted the other way: see
// "NO REAL CONSTRUCTOR" below.
//
// `RtVideo::Context`, `RtVideo::Viewport` and `RtVideo::ConstantBufferOpaque`
// are now ALIASES for the real classes, at the bottom of this file. Nine files
// outside block D1's set spell `RtVideo::Context` (src/engine/engine/loops.cpp,
// graphics/wave21_lane_gfxbody.cpp, graphics/wave27_lane_gfxbig.cpp,
// save/saveload_draw.cpp, ui/spritecontext.cpp, ui/spritecontext_flush.cpp,
// ui/userinterface_draw.cpp, video/video_program.cpp) and they are untouched —
// one definition, an alias, and no duplicate layout (B4b-1).
//
// NOTHING HOLDS `Video::Context` BY VALUE — CHECKED, NOT ASSUMED
// ---------------------------------------------------------------------------
// B35_RULES section 13 item 5 records this as unproven: "Whether anything holds
// `Graphics::Scene` or `Video::Context` by value is NOT established … A block
// converting `Scene` or `Context` must repeat the search over `code/` and
// `standalone/`." Repeated this pass over `code/`, `engine/`, `standalone/`,
// `src/engine/`, `src/engine/`, `src/render/dx11/` and `tests/`, for a by-value member,
// local, array or parameter and for every `sizeof(Context)` / `kContextSize`
// site: **no by-value holder exists.** Every `kContextSize` use is a raw
// `uint8_t[]` scratch subject inside `src/engine/video/context_bind.cpp`'s own
// probe. So `Context` needs neither a protected `= default` constructor nor a
// `friend` holder (B4b-3 / RULE B5-4 do not fire), and it is CONCRETE with no
// constructor at all.
//
// NO REAL CONSTRUCTOR AND NO REAL DESTRUCTOR (B4b-2, B35_RULES 11)
// ---------------------------------------------------------------------------
// `??0Context@Video@@` exists in three spellings — `(const Context&)`
// 0x180018560, `(Device*)` 0x1800f1280 and `(Device*,int,int,int,RenderBuffer*)`
// 0x1800f1820 — and there is NO default constructor. The five-argument one runs
// on 0x2168 raw bytes handed over by `System::MemoryAllocator`
// (`Engine::MainLoop::CreateVideoContext`, 0x1800dd500) and its first act is to
// write `_device`; it builds seven `ConstantBuffer`s in line, four sub-objects
// through four different class `operator new`s, and two default ImageBuffers. It
// stays a registered ctor DOOR (`RtVideo_Context_Ctor`,
// src/engine/video/context_ctor.cpp), and so do `??1` (the destructor,
// `RtVideo_Context_Dtor` in context_lifecycle.cpp), the two `??2` and the `??3`.
// A user-provided constructor, destructor, copy constructor or copy assignment
// here would also destroy `std::is_trivially_copyable<Context>`, which the whole
// of `src/engine/video/` and `src/render/dx11/` relies on.
//
// THERE IS NO VTABLE. The ctor's first store is `*(Device**)this = device`
// (0x1800f1820), not a vtable stamp, and no Context method is virtual.
//
// SIZE IS PROVEN, NOT INFERRED. `Engine::MainLoop::CreateVideoContext`
// (Runtime 0x1800dd500, ordinal 2139) allocates the object itself:
//
//     pCtx = MemoryAllocator::Allocate(_singleton, 0x2168, 8);
//     Video::Context::Context(pCtx, Video::Device::_singleton,
//                             0x800000, 0x400000, 0x100000, nullptr);
//
// so sizeof(Video::Context) == 0x2168 and its alignment is 8.
//
// ---------------------------------------------------------------------------
// EVIDENCE FOR EVERY OFFSET
// ---------------------------------------------------------------------------
// The Context has an unusually complete set of one-line accessors, and a
// one-line accessor is the strongest kind of evidence there is: it gives the
// offset AND the field's real name, straight out of the shipped export table.
// Every named field below cites either such an accessor or the setter that
// owns the field. Retail addresses and ordinals are from the export directory
// of C:\GOG Games\Banished\Runtime-x64.dll (read with pefile, not guessed).
//
//   off     field                     proven by                          retail
//   ------  ------------------------  ---------------------------------  ---------
//   0x0000  _device                   ctor's first store                 0x1800f1820
//   0x0008  _frameIndex               ?GetFrameIndex@                    0x18001ae50
//   0x000c  _shaderDetail             ?GetShaderDetail@                  0x18001ae30
//   0x0010  _dirty                    every setter ORs into it
//   0x0018  _imageBufferDirty         ?SetImageBuffer@ (bit per slot)    0x180019ed0
//   0x001c  _vertexBufferDirty        ?SetVertexBuffer@ (bit per slot)   0x180019f20
//   0x0020  _transformCount           ?SetTransform@ high-water mark     0x18001a6b0
//   0x0028  _renderBuffer             ?GetRenderBuffer@                  0x180019ec0
//   0x0030  _imageBuffers[16]         ?SetImageBuffer@                   0x180019ed0
//   0x00b0  _vertexLayout             ?SetVertexLayout@                  0x180019f00
//   0x00b8  _vertexBuffers[16]        ?SetVertexBuffer@ / ?GetVertexBuffer@
//   0x0138  _vertexOffsets[16]        ?GetVertexBufferOffset@            0x180019fa0
//   0x0178  _vertexStrides[16]        ?GetVertexBufferStride@            0x180019f90
//   0x01b8  _indexBuffer              ?SetIndexBuffer@                   0x180019fb0
//   0x01c0  _indexOffset              ?SetIndexBuffer@ (2nd arg)         0x180019fb0
//   0x01c8  _renderState              ?SetRenderState@                   0x180019fe0
//   0x01d0  _viewport                 ?GetViewport@ / ?SetViewport@      0x18001a0c0
//   0x01e0  _scissor                  ?SetScissor@                       0x18001a0f0
//   0x01f0  _vertexProgram            ?SetVertexProgram@                 0x18001a000
//   0x01f8  _pixelProgram             ?SetPixelProgram@                  0x18001a020
//   0x0200  _videoProgram             ?SetVideoProgram@                  0x18001a040
//   0x0258  _fogColorPixel            ?SetFog@ 2nd copy of the colour    0x18001a360
//   0x0298  _materialConstants        ?GetMaterialConstantBuffer@        0x18001aef0
//   0x02e0  _pixelConstants           ?GetPixelConstantBuffer@           0x18001af00
//   0x0328  _viewProjection           ?SetCamera@ (invView * proj)       0x18001a150
//   0x0368  _inverseView              ?SetCamera@ (Matrix::Inverse)      0x18001a150
//   0x03a8  _cameraPosition           ?SetCamera@ (view row 3)           0x18001a150
//   0x03c8  _time                     ?SetTime@                          0x18001a330
//   0x03d8  _fogParams                ?SetFog@                           0x18001a360
//   0x03e8  _fogColor                 ?SetFog@                           0x18001a360
//   0x03f8  _inverseScale             ?SetInverseScale@                  0x18001a880
//   0x0408  _color                    ?SetColor@                         0x18001a8e0
//   0x10d8  _transforms[52]           ?SetTransform@ (this + i*0x40 + 0x10d8)
//   0x1f78  _globalConstants          ?GetGlobalConstantBuffer@          0x18001aea0
//   0x1fc0  _instanceConstants        ?GetInstanceConstantBuffer@        0x18001aed0
//   0x2008  _boneConstants            ?GetBoneConstantBuffer@            0x18001aec0
//   0x2050  _transformConstants       ?GetTransformConstantBuffer@       0x18001aeb0
//   0x2098  _lightConstants           ?GetLightConstantBuffer@           0x18001aee0
//   0x20e0  _dynamicVertexCursor      ?AddVertex@                        0x18001ad40
//   0x20e8  _dynamicVertexStride      ?AddVertex@                        0x18001ad40
//   0x20f8  _dynamicVertexBuffer      ctor (param 2 > 0 branch)          0x1800f1820
//   0x2110  _instanceCount            src/render/dx11/contextdata.cpp slot 16/18
//   0x2118  _dynamicIndexBuffer       ctor (param 3 > 0 branch)          0x1800f1820
//   0x2130  _dynamicInstanceBuffer    ctor (param 4 > 0 branch)          0x1800f1820
//   0x2138  _defaultRenderBuffer      ?GetDefaultRenderBuffer@           0x18001ae40
//   0x2140  _defaultColorImage        ctor (param 5 == nullptr branch)   0x1800f1820
//   0x2148  _defaultDepthImage        ctor (param 5 == nullptr branch)   0x1800f1820
//   0x2150  _trianglesSubmitted       ?GetTrianglesSubmitted@            0x18001ae60
//   0x2154  _verticesSubmitted        ?GetVerticesSubmitted@             0x18001ae70
//   0x2158  _drawsSubmitted           ?GetDrawsSubmitted@                0x18001ae80
//   0x215c  _objectsSubmitted         ?GetObjectsSubmitted@              0x18001ae90
//   0x2160  _contextData              every backend thunk's first load
//
// The sizes of the seven `ConstantBuffer` members are pinned by the ctor and
// by the getters together: the ctor initialises the pair at +0x298 and +0x2e0
// with the same field-by-field sequence, and 0x2e0 - 0x298 == 0x48; the same
// stride separates the five at +0x1f78 / +0x1fc0 / +0x2008 / +0x2050 / +0x2098.
// So sizeof(Video::ConstantBuffer) == 0x48. Its INTERIOR is now modelled, in
// ConstantBuffer.h — which is the one thing this header no longer says "we have
// not proven this" about.
//
// CROSS-CHECK. src/render/dx11/contextdata.cpp recovered +0x0028, +0x01d0, +0x2110,
// +0x2138, +0x2150..+0x215c, +0x20e0, +0x20e8 and +0x2160 INDEPENDENTLY, from
// the DX11 side, before this lane existed. Every one agrees.
//
// ---------------------------------------------------------------------------
// THE DIRTY MASK (+0x0010)
// ---------------------------------------------------------------------------
// Each bit is named by the setter that sets it. Bits 9..16 (0x1fe00) are the
// constant-buffer group, which is exactly the value `Reset` writes wholesale.
//
//   0x00000001  render buffer      ?SetRenderBuffer@         0x1800f2100
//   0x00000002  image buffers      ?SetImageBuffer@          0x180019ed0
//   0x00000004  vertex layout      ?SetVertexLayout@         0x180019f00
//   0x00000008  vertex buffers     ?SetVertexBuffer@         0x180019f20
//   0x00000010  index buffer       ?SetIndexBuffer@          0x180019fb0
//   0x00000020  render state       ?SetRenderState@          0x180019fe0
//   0x00000040  vertex program     ?SetVertexProgram@        0x18001a000
//   0x00000080  pixel program      ?SetPixelProgram@         0x18001a020
//   0x00000100  VIDEO program      ?SetVideoProgram@         0x18001a040
//   0x00000200  viewport           ?SetViewport@             0x18001a060
//   0x00000400  scissor           ?SetScissor@              0x18001a0f0
//   0x00000800  global constants   ?SetCamera@ / ?SetTime@
//   0x00001000  INSTANCE constants ?SetColor@ / ?SetInverseScale@  (see below)
//   0x00002000  LIGHT constants    the buffer at +0x2098
//   0x00004000  transform consts   ?SetTransform@
//   0x00008000  MATERIAL constants the buffer at +0x298
//   0x00010000  pixel constants    ?SetFog@ (which ORs 0x10800)
//
// Wave 9 lane CTX read `?BindResources@Context@Video@@` (0x1800ef060) — the one
// function that consumes the whole mask — and that is where 0x100, 0x2000 and
// 0x8000 above come from, along with one correction: bit 0x1000 uploads +0x3f8
// length 0x20 (`_inverseScale` and `_color`) into the buffer at +0x1fc0, which
// this header calls `_instanceConstants`. It is kDirtyInstanceConstants (it was
// once misnamed kDirtyMaterialConstants); kDirtyMaterialConstants is 0x8000.
//
// BindResources also settles what +0x20 and +0x24 are for: its transform arm
// fires on `(dirty & 0x4000) || (int32)+0x20 > 0 || (int32)+0x24 > 0`, and its
// final clear is a QWORD store over BOTH. So `_unknown0024` is a live counter
// with the same sign convention as `_transformCount`, not padding.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "../math/Math.h"
#include "ConstantBuffer.h"
#include "ClearFlags.h"       // Video::ClearFlags (Clear's first argument)
// ENDGAME B.3.5 block D2, RULE B5-2.3: the real `Video::RenderBuffer`, which
// `RtVideo::RenderBufferPrefix` below is now an alias of rather than a third view.
#include "Device.h"

namespace Video {

// Forward declarations only, for the member signatures below. Every one of
// these is another block's class (`RenderBuffer`, `Device`, `IndexBuffer`,
// `VertexBuffer` are D2's; `ImageBuffer` and `RenderState` are C's;
// `VertexLayout`, `VertexProgram`, `PixelProgram`, `VideoProgram` are B's;
// `ContextData` is block A's, in CompanionData.h), so this header declares and
// never defines them, and every FIELD stays `void*` — nine files outside this
// block cast those fields to their own partial views and a typed field would
// break every one of those casts.
class ContextData;
class Device;
class ImageBuffer;
class IndexBuffer;
class PixelProgram;
class RenderBuffer;
class RenderState;
class VertexBuffer;
class VertexLayout;
class VertexProgram;
class VideoProgram;

// ============================================================================
// Video::Viewport — {x, y, width, height}, four int32.
// ============================================================================
// Proven by `?SetViewport@` (0x18001a060) and `?SetScissor@` (0x18001a0f0),
// which both compare and copy exactly four dwords, by `?GetViewport@`
// (0x18001a0c0) returning 16 bytes, and by the four-argument constructor
// 0x180018220, read as disassembly this pass: `MOV [RCX],EDX` (x),
// `MOV [RCX+4],R8D` (y), `MOV [RCX+8],R9D` (width), `MOV [RCX+0xc],[RSP+0x28]`
// (height) — so the argument order really is (x, y, width, height).
//
// TRIVIALITY IS THE CONSTRAINT, AND IT IS WHY THE CONSTRUCTORS STAY DOORS
// (RULE B5-4). `Video::Context` holds two Viewports BY VALUE and has no real
// constructor, so `Viewport` must stay trivially copyable AND trivially
// default-constructible. Retail exports both constructors, and the default one
// (0x180018210) is NOT a no-op — `XOR EAX,EAX / MOV [RCX],RAX /
// MOV [RCX+8],RAX` zeroes all four fields. Declaring it would therefore mean
// either a user-provided zeroing body (which makes `Context` non-trivially
// default-constructible, i.e. gives `Context` a constructor B4b-2 forbids) or
// `= default` (which is a DIVERGENCE: retail zeroes, a trivial default ctor
// does not). Neither is acceptable, so both constructors stay registered DOORS
// and this class stays an aggregate — exactly the B4b-2 posture `Context`
// itself takes, for the same reason.
//
// `??4Viewport@Video@@` (0x1800181f0) is a field-by-field copy, i.e. what the
// implicit trivial copy assignment already does; declaring it would destroy
// trivial copyability for no gain, so it is not declared either.
//
// The two comparison operators ARE real members: they change no triviality
// property, their bodies are four dword compares read out of 0x180018240 /
// 0x180018270 this pass, and they are the one part of this class a C++ caller
// can use as retail wrote it.
struct Viewport {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;

    // 0x180018240 — four compares in x, y, width, height order, short-circuit.
    bool operator==(const Viewport& other) const;
    // 0x180018270 — retail emits a SEPARATE body, not `!(a == b)`; the port
    // reproduces its own four compares (see src/engine/video/context.cpp).
    bool operator!=(const Viewport& other) const;
};
static_assert(sizeof(Viewport) == 0x10, "Video::Viewport is 16 bytes");
static_assert(alignof(Viewport) == 4, "Video::Viewport is dword-aligned");
static_assert(std::is_trivially_copyable<Viewport>::value,
              "RULE B5-4: Video::Context holds two Viewports by value and "
              "returns one by value through a hidden retslot door");
static_assert(std::is_trivially_default_constructible<Viewport>::value,
              "RULE B5-4: a non-trivial default ctor here gives Video::Context "
              "a constructor it must not have (B4b-2) — and retail's own "
              "??0Viewport@Video@@QEAA@XZ zeroes, so `= default` would diverge");
static_assert(offsetof(Viewport, x)      == 0x00, "Viewport::x");
static_assert(offsetof(Viewport, y)      == 0x04, "Viewport::y");
static_assert(offsetof(Viewport, width)  == 0x08, "Viewport::width");
static_assert(offsetof(Viewport, height) == 0x0c, "Viewport::height");

// ============================================================================
// Video::Context
// ============================================================================
// No `#pragma pack`: every member is already at its natural alignment, so the
// compiler inserts no padding and `sizeof` lands on 0x2168 on its own. The
// `static_assert`s underneath are what actually prove that, on this compiler.
class Context {
public:
    // ---- ported: the state mutators ------------------------------------
    // Bodies in src/engine/video/context.cpp; each registered door there is a
    // one-line QUALIFIED non-virtual delegation onto the member.
    //
    // Where retail's parameter is a class pointer whose class is another
    // block's, the parameter stays `const void*` and the real type is in the
    // comment: the FIELD is `void*` (see the forward-declaration note above),
    // and a typed parameter would only move the cast from the door into the
    // body. Where the parameter's class is real TODAY — `Math::Matrix`,
    // `Math::Vector4`, `Viewport` — it is spelled.
    void SetColor(const Math::Vector4& color);                     // 0x18001a8e0
    void SetInverseScale(float x, float y, float z, float w);      // 0x18001a880
    void SetTime(float time);                                      // 0x18001a330
    void SetFog(float nearZ, float farZ, const Math::Vector4& color);  // 0x18001a360
    void SetViewport(const Viewport& vp);                          // 0x18001a060
    // B.4: SetVertexBuffer / SetRenderBuffer take retail's types
    // (`PEBVVertexBuffer@2@`, `PEBVRenderBuffer@2@`). B5 CTX (D8): the two
    // `const void*` forwarders dx11 used are gone; dx11 calls these typed.
    void SetImageBuffer(const ImageBuffer* image, int32_t slot);   // 0x180019ed0
    void SetVertexBuffer(const VertexBuffer* vb, int32_t slot,
                         int32_t offset, int32_t stride);          // 0x180019f20
    void SetRenderBuffer(const RenderBuffer* rb);                  // 0x1800f2100
    void SetDefaultRenderBuffer(RenderBuffer* rb);                 // 0x1800ef3c0
    void SetTransform(int32_t slot, const Math::Matrix& m);        // 0x18001a6b0
    void SetTransformScale(int32_t slot, const Math::Matrix& m,
                           const Math::Vector4& scale);            // 0x18001a400
    void SetCamera(const Math::Matrix& view,
                   const Math::Matrix& projection);                // 0x18001a150
    void SetBones(int32_t count, const Math::Matrix* matrices);    // 0x18001a780
    // 0x18001a950 `?SetLight@Context@Video@@QEAAXAEBVVector3@Math@@0AEBVVector4@4@1MMH@Z`:
    // (const Vector3& direction, const Vector3& attenuation, const Vector4& colour,
    // const Vector4& position, float, float, int). The two Vector3s are read as
    // three floats; the two Vector4s are COPIED as four dwords (no arithmetic).
    void SetLight(const Math::Vector3& direction, const Math::Vector3& attenuation,
                  const Math::Vector4& colour, const Math::Vector4& position,
                  float nearPlane, float farPlane,
                  int32_t shadowResolution);
    void Reset();                                                  // 0x1800f1e50
    void ClearResources();                                         // 0x1800f2090
    void AddVertex(const void* vertex);                            // 0x18001ad40
    void AddVertices(int32_t count, const void* vertices);         // 0x18001ad70

    // ---- ported: the two private bind halves the frame loop drives -----
    // Bodies in src/engine/video/context_bind.cpp. `AEAA` in the mangled name:
    // both are PRIVATE in retail, and a `QEAA` guess resolves NULL in silence
    // (reference_resolveexport_phantom_names.md). Public here because the two
    // registered doors and the `src/render/dx11/` backend reach them from outside.
    void BindResources();                                          // 0x1800ef060
    void BindTextures();                                           // 0x1800ef330

    // ---- ported: the private constant-buffer declaration pass ----------
    // Body in src/engine/video/constant_buffer.cpp: the engine's whole shader
    // constant layout, seven ConstantBuffers and 31 declarations.
    void AssignConstants();                                        // 0x1800ef3d0

    // ---- ported: the nine backend forwarders ---------------------------
    // Each is a three-instruction tail call through the `ContextData` vtable at
    // the byte offset in the `kSlot*` enum below. Bodies in context.cpp.
    void Clear(ClearFlags flags, uint32_t color, float depth, int32_t stencil);  // 0x1800f23f0
    void DrawIndexed(int32_t primitiveType, int32_t a, int32_t b,
                     int32_t c, int32_t d, int32_t e);             // 0x1800f2410
    int32_t GetMaxDynamicInstances(int32_t a, int32_t b, int32_t c) const;     // 0x1800f21d0
    bool BeginVertices(int32_t count, int32_t stride);              // 0x1800f21b0
    void EndVertices(int32_t primitiveType);                       // 0x1800f2190
    bool BeginIndexedVertices(int32_t a, int32_t b, int32_t c);     // 0x1800f2170
    void EndIndexedVertices(int32_t primitiveType);                // 0x1800f2150
    bool BeginInstancedIndexedVertices(int32_t a, int32_t b,
                                       int32_t c, int32_t d);      // 0x1800f2130
    void EndInstancedIndexedVertices(int32_t primitiveType);       // 0x1800f2110

    // ---- exported, NOT ported ------------------------------------------
    // All 85 `Video::Context` exports are accounted for: 30 above, 47 here and
    // the eight constructors / destructor / `operator new` / `operator delete`
    // spellings in the comment that follows. Declared and left undefined so no
    // later block has to edit this header (BLOCK_BRIEF "the shape"); nothing in
    // the tree calls any of them as a member, and the rows exist today only in
    // `standalone/rt_export_addrs.inc`.
    void AddIndex(uint16_t index);                                 // 0x18001adc0 — not ported
    void AddIndices(int32_t count, const uint16_t* indices);       // 0x18001ade0 — not ported
    bool BeginCommands();                                          // 0x1800f2340 — not ported
    void EndCommands();                                            // 0x1800f2360 — not ported
    void Finish();                                                 // 0x1800f2380 — not ported
    void ForceRenderStateDirty(uint32_t bits);                     // 0x180019eb0 — not ported
    // The seven private one-line bind thunks `BindResources` dispatches
    // through. Each is `MOV RCX,[RCX+0x2160] / MOV RAX,[RCX] / JMP [RAX+off]`,
    // and the offsets are the `kSlotBind*` values context_bind.cpp already
    // uses; the ported `BindResources` reaches the backend directly, exactly as
    // retail does, rather than through these.
    void BindRenderBuffer();                                       // 0x1800f2280 — not ported (AEAA)
    void BindViewport();                                           // 0x1800f2300 — not ported (AEAA)
    void BindScissor();                                            // 0x1800f2320 — not ported (AEAA)
    void BindTransforms();                                         // 0x1800f22a0 — not ported (AEAA)
    void BindTextureNone(int32_t slot);                            // 0x1800f2260 — not ported (AEAA)
    void BindIndexNone();                                          // 0x1800f22e0 — not ported (AEAA)
    void BindVertexNone(int32_t slot);                             // 0x1800f22c0 — not ported (AEAA)
    void PlatformConstruct();                                      // 0x1800f21f0 — not ported (AEAA)
    void PlatformDestruct();                                       // 0x1800f2240 — not ported (AEAA)
    // The seven embedded constant buffers, by name. Each is a `LEA` on `this`.
    const ConstantBuffer& GetGlobalConstantBuffer() const;         // 0x18001aea0 — not ported
    const ConstantBuffer& GetInstanceConstantBuffer() const;       // 0x18001aed0 — not ported
    const ConstantBuffer& GetBoneConstantBuffer() const;           // 0x18001aec0 — not ported
    const ConstantBuffer& GetTransformConstantBuffer() const;      // 0x18001aeb0 — not ported
    const ConstantBuffer& GetLightConstantBuffer() const;          // 0x18001aee0 — not ported
    const ConstantBuffer& GetMaterialConstantBuffer() const;       // 0x18001aef0 — not ported
    const ConstantBuffer& GetPixelConstantBuffer() const;          // 0x18001af00 — not ported
    const ConstantBuffer* GetConstantBufferForName(const char* name) const;  // 0x1800f1090 — not ported
    RenderBuffer*       GetDefaultRenderBuffer();                  // 0x18001ae40 — not ported
    const RenderBuffer* GetRenderBuffer() const;                   // 0x180019ec0 — not ported
    const VertexBuffer* GetVertexBuffer(int32_t slot) const;       // 0x180019f80 — not ported
    int32_t GetVertexBufferOffset(int32_t slot) const;             // 0x180019fa0 — not ported
    int32_t GetVertexBufferStride(int32_t slot) const;             // 0x180019f90 — not ported
    int32_t GetFrameIndex() const;                                 // 0x18001ae50 — not ported
    int32_t GetShaderDetail() const;                               // 0x18001ae30 — not ported
    void    SetShaderDetail(int32_t detail);                       // 0x18001ae20 — not ported
    int32_t GetTrianglesSubmitted() const;                         // 0x18001ae60 — not ported
    int32_t GetVerticesSubmitted() const;                          // 0x18001ae70 — not ported
    int32_t GetDrawsSubmitted() const;                             // 0x18001ae80 — not ported
    int32_t GetObjectsSubmitted() const;                           // 0x18001ae90 — not ported
    // A HIDDEN-RETSLOT door (B35_RULES 2.5): `?GetViewport@Context@Video@@
    // QEBA?AVViewport@2@XZ` returns the 16-byte value in the caller's slot. It
    // is one of the 60 by-value-returning Video/Graphics exports and it is NOT
    // one of the two vtable-slot ones, so no slot typedef depends on it.
    //
    // RULE B5-12 AS AMENDED (main c275c5c4): GCC's ms_abi passes the hidden
    // return pointer BEFORE `this`, retail passes `this` first, so a VIRTUAL
    // that retail returns by value must be declared `T* M(T* ret, args…)` in
    // retail order. This one is NON-virtual, so the by-value declaration is
    // allowed — but only for C++ callers. **The day a door is written for it,
    // that door must keep the explicit `(this, ret)` signature and
    // placement-new the member's prvalue into the caller's slot.** There is no
    // door today: the member is unported and nothing in the tree calls it.
    Viewport GetViewport() const;                                  // 0x18001a0c0 — not ported
    void SetScissor(const Viewport& vp);                           // 0x18001a0f0 — not ported
    void SetIndexBuffer(const void* ib, int32_t offset);           // 0x180019fb0 — not ported
    void SetRenderState(const void* rs);                           // 0x180019fe0 — not ported
    void SetVertexLayout(const void* layout);                      // 0x180019f00 — not ported
    void SetVertexProgram(const void* program);                    // 0x18001a000 — not ported
    void SetPixelProgram(const void* program);                     // 0x18001a020 — not ported
    void SetVideoProgram(const void* program);                     // 0x18001a040 — not ported
    void SetMaterialParameter(int32_t index, const Math::Vector4& v);      // 0x18001acb0 — not ported
    void SetUserPosition(const Math::Vector4& p);                  // 0x18001a2e0 — not ported
    void SetTextureProjection(const Math::Matrix& m);              // 0x18001ab80 — not ported
    void SetShadowProjection(int32_t index, const Math::Matrix& m);// 0x18001ac20 — not ported

    // NOT DECLARED, DELIBERATELY — the eight lifecycle spellings. Declaring any
    // of them would make this class non-trivially-copyable (and the
    // constructors non-trivially-default-constructible), which is the property
    // the whole of src/engine/video/ and src/render/dx11/ relies on; and B4b-2 keeps
    // them as registered DOORS anyway. Recorded here so the census can see that
    // all 85 exports were read, not 77:
    //   ??0Context@Video@@QEAA@AEBV01@@Z                    0x180018560  copy ctor, unported
    //   ??0Context@Video@@QEAA@PEAVDevice@1@@Z              0x1800f1280  1-arg ctor, unported
    //   ??0Context@Video@@QEAA@PEAVDevice@1@HHHPEAVRenderBuffer@1@@Z
    //                                                       0x1800f1820  ctor DOOR, context_ctor.cpp
    //   ??1Context@Video@@QEAA@XZ                           0x1800f16e0  dtor DOOR, context_lifecycle.cpp
    //   ??2Context@Video@@SAPEAX_K@Z                        0x180018480  operator new, unported
    //   ??2Context@Video@@SAPEAX_KPEAX@Z                    0x1800184f0  placement new, unported
    //   ??3Context@Video@@SAXPEAX@Z                         0x180018500  operator delete DOOR,
    //                                                                    context_lifecycle.cpp
    //   ??4Context@Video@@QEAAAEAV01@AEBV01@@Z              0x180018f90  operator=, unported

    // ---- the 0x2168 bytes ----------------------------------------------
    // Public for the same reason `Object::External`'s are: the registered doors,
    // the probes and nine files outside this block read and write them by name.
    // B.4 (R-VIDEO), H4: the pointer members carry the classes the setters'
    // and getters' mangled names give them (see the member declarations above).
    Device*  _device;                                   // 0x0000
    int32_t  _frameIndex;                               // 0x0008
    int32_t  _shaderDetail;                             // 0x000c
    uint32_t _dirty;                                    // 0x0010
    uint32_t _unknown0014;                              // 0x0014
    uint32_t _imageBufferDirty;                         // 0x0018
    uint32_t _vertexBufferDirty;                        // 0x001c
    int32_t  _transformCount;                           // 0x0020
    int32_t  _unknown0024;                              // 0x0024
    // Retail: const RenderBuffer*, const ImageBuffer*[16] (the setters'
    // mangled names, `PEBVRenderBuffer@2@` / `PEBVImageBuffer@2@`).
    const RenderBuffer* _renderBuffer;                  // 0x0028
    const ImageBuffer*  _imageBuffers[16];              // 0x0030
    const VertexLayout* _vertexLayout;                  // 0x00b0
    const VertexBuffer* _vertexBuffers[16];             // 0x00b8
    int32_t  _vertexOffsets[16];                        // 0x0138
    int32_t  _vertexStrides[16];                        // 0x0178
    // B.4 (R-GFX), H4: `const IndexBuffer*`, the class `?SetIndexBuffer@Context@Video@@
    // QEAAXPEBVIndexBuffer@2@H@Z` (0x180019fb0) stores here.
    const IndexBuffer* _indexBuffer;                    // 0x01b8
    int32_t  _indexOffset;                              // 0x01c0
    int32_t  _unknown01c4;                              // 0x01c4
    const RenderState* _renderState;                    // 0x01c8
    Viewport _viewport;                                 // 0x01d0
    Viewport _scissor;                                  // 0x01e0
    const VertexProgram* _vertexProgram;                // 0x01f0
    const PixelProgram*  _pixelProgram;                 // 0x01f8

    // 0x0200 .. 0x0297. Wave 9 lane CTX: THE FIRST EIGHT BYTES OF THIS ARRAY
    // ARE A POINTER — Context+0x200 is `_videoProgram`, which
    // `?SetVideoProgram@Context@Video@@` (0x18001a040) compares against its
    // argument, ORs 0x100 into the dirty mask and stores, and which
    // `?BindResources@` dereferences twice. The STAGING is 0x208..0x297, which
    // is exactly the two ranges `?Reset@` memsets (0x208 len 0x40, 0x248 len
    // 0x50) and exactly the two ranges `BindResources` uploads. The member is
    // B4 wave X round 6: the block is a POD of its three parts (plain float
    // arrays, no constructor: Context's ctor memsets the whole 0x98 bytes,
    // context_ctor.cpp). The sixteen-byte rows of `pixel` are, by writer:
    // [0] +0x248 SetLight's shadow dwords, [1] +0x258 `?SetFog@`'s second copy
    // of the fog colour, [2] +0x268 SetLight's colour, [3] +0x278 SetLight's
    // position, [4] +0x288 the per-frame vector (loops.cpp's time,
    // gameupdateloop / menudialogloopt / minimap_paint).
    struct PixelConstantStaging {
        const VideoProgram* videoProgram;   // +0x00 (ctx +0x200)  SetVideoProgram 0x18001a040
        float material[4][4];               // +0x08 (ctx +0x208)  Reset memsets 0x40; uploaded to _materialConstants
        // ENDGAME B7 (CTOR, D6): one Math::Vector4 per sixteen-byte row (was
        // float[5][4]). Its zeroing ctor (0x18001e0a0) never runs here: Context is
        // built over raw storage by RtVideo_Context_Ctor, whose memset of the whole
        // block is retail's nineteen zero qwords; a row store is one 16-byte MOVUPS.
        Math::Vector4 pixel[5];             // +0x48 (ctx +0x248)  Reset memsets 0x50; uploaded to _pixelConstants
    };
    PixelConstantStaging _pixelConstantStaging;          // 0x0200

    ConstantBuffer _materialConstants;                  // 0x0298
    ConstantBuffer _pixelConstants;                     // 0x02e0

    Math::Matrix   _viewProjection;                     // 0x0328
    Math::Matrix   _inverseView;                        // 0x0368
    Math::Vector4  _cameraPosition;                     // 0x03a8
    Math::Vector4  _unknown03b8;                        // 0x03b8
    Math::Vector4  _time;                               // 0x03c8
    Math::Vector4  _fogParams;                          // 0x03d8
    Math::Vector4  _fogColor;                           // 0x03e8
    Math::Vector4  _inverseScale;                       // 0x03f8
    Math::Vector4  _color;                              // 0x0408

    // 0x0418 .. 0x10d7, one 0xcc0-byte block `?Reset@` clears in a single
    // memset. `?SetBones@` (0x18001a780), `?SetShadowProjection@` (0x18001ac20)
    // and `?SetTextureProjection@` (0x18001ab80) write in here. The Context
    // ctor's third sub-object zero-fill covers it as 51 x 0x40 (0x180053840).
    uint8_t  _boneAndProjectionStaging[0xcc0];          // 0x0418

    Math::Matrix _transforms[52];                       // 0x10d8

    // 0x1dd8 .. 0x1f77, one 0x1a0-byte block `?Reset@` clears in a single
    // memset and `BindResources` uploads as one source. `?SetLight@`
    // (0x18001a950) writes its first 0x60 bytes; B.4 (R-VIDEO) names them as
    // SetLight's stores do (batchc_context_setlight.cpp's banner). The rest is
    // never written by a ported body and stays bytes.
    struct LightStaging {
        uint32_t colour[4];        // +0x00  copied as dwords, never float arithmetic
        uint32_t position[4];      // +0x10  likewise
        float    direction[4];     // +0x20  xyz, w = 1.0f
        float    attenuation[3];   // +0x30
        float    attenuationDot;   // +0x3c  (att.y*dir.y + att.x*dir.x) + att.z*dir.z
        float    farPlane;         // +0x40
        float    negNearPlane;     // +0x44  -nearPlane, by sign-mask XOR
        float    invRange;         // +0x48  1.0f / (far - near)
        float    zero4c;           // +0x4c
        float    shadow[4];        // +0x50  1.0f / shadowResolution, 0, 0, 0
        uint8_t  _unwritten60[0x40];   // +0x60
        // +0xa0 (Context+0x1e78): the shadow matrix the Graphics lights'
        // WriteShadowMatrix stores (graphics/lights_vtable.cpp).
        Math::Matrix shadowMatrix;     // +0xa0
        uint8_t  _unwrittenE0[0xc0];   // +0xe0
    };
    LightStaging _lightStaging;                         // 0x1dd8

    ConstantBuffer _globalConstants;                    // 0x1f78
    ConstantBuffer _instanceConstants;                  // 0x1fc0
    ConstantBuffer _boneConstants;                      // 0x2008
    ConstantBuffer _transformConstants;                 // 0x2050
    ConstantBuffer _lightConstants;                     // 0x2098

    uint8_t* _dynamicVertexCursor;                      // 0x20e0
    int32_t  _dynamicVertexStride;                      // 0x20e8
    int32_t  _unknown20ec;                              // 0x20ec
    // The dynamic INDEX cursor: the backend (dx11 contextdata.cpp
    // kDynIndexCursor) maps it, the geometry writers store 16-bit indices
    // through it and step it (wave27_lane_gfxpart.cpp: `ADD R14W,4`, word
    // stores).
    uint16_t* _dynamicIndexCursor;                      // 0x20f0
    VertexBuffer* _dynamicVertexBuffer;                 // 0x20f8
    // B5 CTX: 0x2100..0x212f were two qwords per stream; the dx11 backend
    // (ContextData slots 14-19) reads and writes them as these dword / pointer
    // halves. Named by offset only -- no retail accessor names them. What the
    // backend keeps in each:
    //   0x2100 / 0x2120  int32  byte offset {Vertex,Index}Buffer::Lock writes
    //                           through its `int&` out-parameter
    //   0x2104 / 0x2124  int32  vertex count (BeginVertices) / index count
    //   0x2108 / 0x2128  void*  a copy of the Lock result (the mapped base)
    int32_t  _unknown2100;                              // 0x2100
    int32_t  _unknown2104;                              // 0x2104
    void*    _unknown2108;                              // 0x2108
    int32_t  _instanceCount;                            // 0x2110
    int32_t  _unknown2114;                              // 0x2114
    IndexBuffer* _dynamicIndexBuffer;                   // 0x2118
    int32_t  _unknown2120;                              // 0x2120
    int32_t  _unknown2124;                              // 0x2124
    void*    _unknown2128;                              // 0x2128
    VertexBuffer* _dynamicInstanceBuffer;               // 0x2130
    RenderBuffer* _defaultRenderBuffer;                 // 0x2138
    ImageBuffer*  _defaultColorImage;                   // 0x2140
    ImageBuffer*  _defaultDepthImage;                   // 0x2148
    int32_t  _trianglesSubmitted;                       // 0x2150
    int32_t  _verticesSubmitted;                        // 0x2154
    int32_t  _drawsSubmitted;                           // 0x2158
    int32_t  _objectsSubmitted;                         // 0x215c
    ContextData* _contextData;                          // 0x2160
};

// Every offset the ported bodies touch, asserted. A mismatch here is a compile
// error rather than a wild write into a live render context.
static_assert(offsetof(Context, _device)                == 0x0000, "Context::_device");
static_assert(offsetof(Context, _frameIndex)            == 0x0008, "Context::_frameIndex");
static_assert(offsetof(Context, _shaderDetail)          == 0x000c, "Context::_shaderDetail");
static_assert(offsetof(Context, _dirty)                 == 0x0010, "Context::_dirty");
static_assert(offsetof(Context, _imageBufferDirty)      == 0x0018, "Context::_imageBufferDirty");
static_assert(offsetof(Context, _vertexBufferDirty)     == 0x001c, "Context::_vertexBufferDirty");
static_assert(offsetof(Context, _transformCount)        == 0x0020, "Context::_transformCount");
static_assert(offsetof(Context, _unknown0024)           == 0x0024, "Context::_unknown0024 (BindResources' second counter)");
static_assert(offsetof(Context, _renderBuffer)          == 0x0028, "Context::_renderBuffer");
static_assert(offsetof(Context, _imageBuffers)          == 0x0030, "Context::_imageBuffers");
static_assert(offsetof(Context, _dynamicIndexCursor)    == 0x20f0, "Context::_dynamicIndexCursor");
static_assert(offsetof(Context, _vertexLayout)          == 0x00b0, "Context::_vertexLayout");
static_assert(offsetof(Context, _vertexBuffers)         == 0x00b8, "Context::_vertexBuffers");
static_assert(offsetof(Context, _vertexOffsets)         == 0x0138, "Context::_vertexOffsets");
static_assert(offsetof(Context, _vertexStrides)         == 0x0178, "Context::_vertexStrides");
static_assert(offsetof(Context, _indexBuffer)           == 0x01b8, "Context::_indexBuffer");
static_assert(offsetof(Context, _indexOffset)           == 0x01c0, "Context::_indexOffset");
static_assert(offsetof(Context, _renderState)           == 0x01c8, "Context::_renderState");
static_assert(offsetof(Context, _viewport)              == 0x01d0, "Context::_viewport");
static_assert(offsetof(Context, _scissor)               == 0x01e0, "Context::_scissor");
static_assert(offsetof(Context, _vertexProgram)         == 0x01f0, "Context::_vertexProgram");
static_assert(offsetof(Context, _pixelProgram)          == 0x01f8, "Context::_pixelProgram");
static_assert(offsetof(Context, _pixelConstantStaging)  == 0x0200, "Context::_pixelConstantStaging");
static_assert(sizeof(Context::PixelConstantStaging) == 0x98, "Context +0x200 .. +0x297");
static_assert(offsetof(Context::PixelConstantStaging, videoProgram) == 0x00, "Context +0x200 _videoProgram");
static_assert(offsetof(Context::PixelConstantStaging, material)     == 0x08, "Context +0x208 material staging (Reset 0x208 len 0x40)");
static_assert(offsetof(Context::PixelConstantStaging, pixel)        == 0x48, "Context +0x248 pixel staging (Reset 0x248 len 0x50)");
static_assert(offsetof(Context, _materialConstants)     == 0x0298, "Context::_materialConstants");
static_assert(offsetof(Context, _pixelConstants)        == 0x02e0, "Context::_pixelConstants");
static_assert(offsetof(Context, _viewProjection)        == 0x0328, "Context::_viewProjection");
static_assert(offsetof(Context, _inverseView)           == 0x0368, "Context::_inverseView");
static_assert(offsetof(Context, _cameraPosition)        == 0x03a8, "Context::_cameraPosition");
static_assert(offsetof(Context, _time)                  == 0x03c8, "Context::_time");
static_assert(offsetof(Context, _fogParams)             == 0x03d8, "Context::_fogParams");
static_assert(offsetof(Context, _fogColor)              == 0x03e8, "Context::_fogColor");
static_assert(offsetof(Context, _inverseScale)          == 0x03f8, "Context::_inverseScale");
static_assert(offsetof(Context, _color)                 == 0x0408, "Context::_color");
static_assert(offsetof(Context, _boneAndProjectionStaging) == 0x0418, "Context::_boneAndProjectionStaging");
static_assert(offsetof(Context, _transforms)            == 0x10d8, "Context::_transforms");
static_assert(offsetof(Context, _lightStaging)          == 0x1dd8, "Context::_lightStaging");
static_assert(sizeof(Context::LightStaging) == 0x1a0, "Context::LightStaging is 0x1a0 bytes");
static_assert(offsetof(Context::LightStaging, colour) == 0x0, "LightStaging::colour");
static_assert(offsetof(Context::LightStaging, position)       == 0x10, "LightStaging::position");
static_assert(offsetof(Context::LightStaging, direction)      == 0x20, "LightStaging::direction");
static_assert(offsetof(Context::LightStaging, attenuation)    == 0x30, "LightStaging::attenuation");
static_assert(offsetof(Context::LightStaging, attenuationDot) == 0x3c, "LightStaging::attenuationDot");
static_assert(offsetof(Context::LightStaging, farPlane)       == 0x40, "LightStaging::farPlane");
static_assert(offsetof(Context::LightStaging, negNearPlane)   == 0x44, "LightStaging::negNearPlane");
static_assert(offsetof(Context::LightStaging, invRange)       == 0x48, "LightStaging::invRange");
static_assert(offsetof(Context::LightStaging, zero4c)         == 0x4c, "LightStaging::zero4c");
static_assert(offsetof(Context::LightStaging, shadow)         == 0x50, "LightStaging::shadow");
static_assert(offsetof(Context::LightStaging, _unwritten60)   == 0x60, "LightStaging +0x60");
static_assert(offsetof(Context::LightStaging, shadowMatrix)   == 0xa0, "LightStaging::shadowMatrix (Context+0x1e78)");
static_assert(offsetof(Context::LightStaging, _unwrittenE0)   == 0xe0, "LightStaging tail");
static_assert(offsetof(Context, _globalConstants)       == 0x1f78, "Context::_globalConstants");
static_assert(offsetof(Context, _instanceConstants)     == 0x1fc0, "Context::_instanceConstants");
static_assert(offsetof(Context, _boneConstants)         == 0x2008, "Context::_boneConstants");
static_assert(offsetof(Context, _transformConstants)    == 0x2050, "Context::_transformConstants");
static_assert(offsetof(Context, _lightConstants)        == 0x2098, "Context::_lightConstants");
static_assert(offsetof(Context, _dynamicVertexCursor)   == 0x20e0, "Context::_dynamicVertexCursor");
static_assert(offsetof(Context, _dynamicVertexStride)   == 0x20e8, "Context::_dynamicVertexStride");
static_assert(offsetof(Context, _dynamicVertexBuffer)   == 0x20f8, "Context::_dynamicVertexBuffer");
static_assert(offsetof(Context, _instanceCount)         == 0x2110, "Context::_instanceCount");
static_assert(offsetof(Context, _dynamicIndexBuffer)    == 0x2118, "Context::_dynamicIndexBuffer");
static_assert(offsetof(Context, _dynamicInstanceBuffer) == 0x2130, "Context::_dynamicInstanceBuffer");
static_assert(offsetof(Context, _defaultRenderBuffer)   == 0x2138, "Context::_defaultRenderBuffer");
static_assert(offsetof(Context, _defaultColorImage)     == 0x2140, "Context::_defaultColorImage");
static_assert(offsetof(Context, _defaultDepthImage)     == 0x2148, "Context::_defaultDepthImage");
static_assert(offsetof(Context, _trianglesSubmitted)    == 0x2150, "Context::_trianglesSubmitted");
static_assert(offsetof(Context, _verticesSubmitted)     == 0x2154, "Context::_verticesSubmitted");
static_assert(offsetof(Context, _drawsSubmitted)        == 0x2158, "Context::_drawsSubmitted");
static_assert(offsetof(Context, _objectsSubmitted)      == 0x215c, "Context::_objectsSubmitted");
static_assert(offsetof(Context, _contextData)           == 0x2160, "Context::_contextData");

// The allocation size in Engine::MainLoop::CreateVideoContext (0x1800dd500).
static_assert(sizeof(Context) == 0x2168, "sizeof(Video::Context) == 0x2168");
static_assert(offsetof(Context, _unknown0014) == 0x14, "Context::_unknown0014");
static_assert(offsetof(Context, _unknown01c4) == 0x1C4, "Context::_unknown01c4");
static_assert(offsetof(Context, _unknown03b8) == 0x3B8, "Context::_unknown03b8");
static_assert(offsetof(Context, _unknown20ec) == 0x20EC, "Context::_unknown20ec");
static_assert(offsetof(Context, _unknown2100) == 0x2100, "Context::_unknown2100");
static_assert(offsetof(Context, _unknown2104) == 0x2104, "Context::_unknown2104");
static_assert(offsetof(Context, _unknown2108) == 0x2108, "Context::_unknown2108");
static_assert(offsetof(Context, _unknown2114) == 0x2114, "Context::_unknown2114");
static_assert(offsetof(Context, _unknown2120) == 0x2120, "Context::_unknown2120");
static_assert(offsetof(Context, _unknown2124) == 0x2124, "Context::_unknown2124");
static_assert(offsetof(Context, _unknown2128) == 0x2128, "Context::_unknown2128");
static_assert(alignof(Context) == 8, "Video::Context is qword-aligned (MemAlloc's align 8)");

// The property that makes the 0x2168-byte block a flat block: nine files read
// and write it through `memcpy`, `memset` and `reinterpret_cast`, and
// context_bind.cpp's probe compares every one of the 0x2168 bytes.
static_assert(std::is_trivially_copyable<Context>::value,
              "Video::Context must stay trivially copyable: no user-provided "
              "ctor, dtor, copy ctor or copy assignment (B4b-2 keeps all four "
              "as registered doors)");
static_assert(std::is_standard_layout<Context>::value,
              "Video::Context must stay standard-layout: every offsetof above "
              "and every reinterpret_cast onto MemAlloc'd bytes depends on it");

}  // namespace Video

// ============================================================================
// The `RtVideo` compatibility layer — ALIASES, not a second layout (B4b-1)
// ============================================================================
// `Video::Context`, `Video::Viewport` and `Video::ConstantBuffer` now own the
// retail names. These aliases keep the pre-B.3.5 spellings working so the nine
// files outside block D1's set that use them are untouched; they die with the
// caller pass. There is exactly ONE definition of each layout.
namespace RtVideo {

using Context              = ::Video::Context;
using Viewport             = ::Video::Viewport;
using ConstantBufferOpaque = ::Video::ConstantBuffer;

// Math::Matrix / Math::Vector4 replaced `MatrixRaw` / `Vector4Raw` in B.3.5:
// the two pairs were the same 64 and 16 bytes, both are trivially copyable, and
// `src/engine/video/context.cpp`'s three bridge helpers
// (`MatrixInverse`, `MatrixMul`, `MatrixNotEqual`) and their
// `sizeof(MatrixRaw) == sizeof(Math::Matrix)` bridge assert are gone with them.
// The aliases are kept because a grep for either name should land here rather
// than on nothing.
using MatrixRaw  = ::Math::Matrix;
using Vector4Raw = ::Math::Vector4;

// ----------------------------------------------------------------------------
// Video::RenderBuffer, only as far as `?Reset@` reads it: Reset (0x1800f1e50)
// loads *(int*)(rb + 0x10) and *(int*)(rb + 0x14) as the width/height it resets
// the viewport and scissor to.
//
// *** RULE B5-2.3: COLLAPSED. This was the THIRD OF THREE VIEWS OF ONE CLASS,
// and it is the last of the three to go. Block A deleted dx11's
// `RenderBufferOwner`'s false `static_assert(sizeof == 0x10)`; ENDGAME B.3.5
// block D2 made `Video::RenderBuffer` a real 0x30-byte class
// (src/engine/video/Device.h) and turned that dx11 view into an alias of it;
// this prefix view is now an alias too, so ONE definition of the layout remains
// where there were three.
//
// ZERO CALLERS CHANGE, which is what made it safe to do here rather than in a
// caller batch: all four consumers read `rb->width` / `rb->height`, and those
// are real fields of the real class at exactly +0x10 and +0x14. They are
// `?Reset@Context@` (src/engine/video/context.cpp),
// `src/engine/graphics/wave27_lane_gfxbig.cpp`,
// `src/engine/save/saveload_draw.cpp` and `src/engine/ui/spritecontext.cpp`;
// three of those four are outside both b35-D1's and b35-D2's file sets and are
// untouched. Landed in b35-D2 with b35-D1's agreement on the record, because
// b35-D1 was already merged and adding this include there would have
// manufactured a merge-order dependency.
//
// STILL OPEN, both handed to the closing pass: two more duplicates of
// `Video::Viewport` itself, at src/engine/ui/clipregionelement.cpp:73
// (`struct Viewport { int32_t x, y, w, h; }`) and src/render/dx11/contextdata.cpp:425.
// ----------------------------------------------------------------------------
using RenderBufferPrefix = ::Video::RenderBuffer;
static_assert(offsetof(RenderBufferPrefix, width)  == 0x10, "RenderBuffer::width");
static_assert(offsetof(RenderBufferPrefix, height) == 0x14, "RenderBuffer::height");
static_assert(sizeof(RenderBufferPrefix) == 0x30,
              "RULE B5-2.3: the real class, not a 0x18-byte prefix view");

// The number of transform slots is 0xd00 / 0x40 = 52, from the byte count
// Reset (0x1800f1e50) memsets over the array at +0x10d8.
constexpr int kTransformSlots  = 52;
// Sixteen texture and sixteen vertex-stream slots: ClearResources (0x1800f2090)
// and Reset both walk exactly 0x10 iterations over each array.
constexpr int kImageBufferSlots  = 16;
constexpr int kVertexBufferSlots = 16;
// Context::_imageBufferDirty (+0x18) is one bit per texture slot: every slot dirty.
constexpr uint32_t kAllImageBufferSlotsDirty = 0xffffu;   // (1u << kImageBufferSlots) - 1

// ---- Dirty-mask bits (named by their setters; see the banner) ---------------
// Context::_dirty (+0x10): what BindResources (0x1800ef060) must re-bind or re-upload.
// Unscoped so `ctx->_dirty |= RtVideo::kDirtyX` stays an integer expression.
enum ContextDirty : uint32_t {
    kDirtyRenderBuffer      = 0x00000001u,
    kDirtyImageBuffers      = 0x00000002u,
    kDirtyVertexLayout      = 0x00000004u,
    kDirtyVertexBuffers     = 0x00000008u,
    kDirtyIndexBuffer       = 0x00000010u,
    kDirtyRenderState       = 0x00000020u,
    kDirtyVertexProgram     = 0x00000040u,
    kDirtyPixelProgram      = 0x00000080u,
    kDirtyVideoProgram      = 0x00000100u,
    kDirtyViewport          = 0x00000200u,
    kDirtyScissor           = 0x00000400u,
    kDirtyGlobalConstants   = 0x00000800u,
    // The INSTANCE constants: BindResources uploads +0x3f8 length 0x20
    // (`_inverseScale` and `_color`) into the buffer at +0x1fc0 (context_bind.cpp).
    // Formerly misnamed kDirtyMaterialConstants; see the banner.
    kDirtyInstanceConstants = 0x00001000u,
    kDirtyLightConstants    = 0x00002000u,
    kDirtyTransformConstants= 0x00004000u,
    // The REAL material constants: the buffer at +0x298 (staging +0x208),
    // BindResources' upload {0x08000, &_materialConstants}.
    kDirtyMaterialConstants = 0x00008000u,
    kDirtyPixelConstants    = 0x00010000u,
    // The literal Reset (0x1800f1e50) stores into +0x10 before it starts
    // re-dirtying individual bindings. Bits 9..16 inclusive.
    kDirtyAllConstants      = 0x0001fe00u,
};

constexpr size_t kContextSize = sizeof(::Video::Context);

// ---------------------------------------------------------------------------
// Video::ContextData vtable byte offsets
// ---------------------------------------------------------------------------
// Nine Context methods are pure tail-jumps into the backend object at +0x2160:
//
//     MOV RCX,[RCX+0x2160] / MOV RAX,[RCX] / JMP [RAX + <offset>]
//
// The offsets below are read out of those three-instruction bodies. They agree
// exactly with the twenty-slot table src/render/dx11/contextdata.cpp recovered from
// the VideoDX11 side, and with `Video::ContextData`'s declared slot order in
// src/engine/video/CompanionData.h (ENDGAME B.3.5 block A).
enum : unsigned {
    kSlotClear                         = 0x20,
    kSlotDrawIndexed                   = 0x60,
    kSlotGetMaxDynamicInstances        = 0x68,
    kSlotBeginVertices                 = 0x70,
    kSlotEndVertices                   = 0x78,
    kSlotBeginIndexedVertices          = 0x80,
    kSlotEndIndexedVertices            = 0x88,
    kSlotBeginInstancedIndexedVertices = 0x90,
    kSlotEndInstancedIndexedVertices   = 0x98,
};

}  // namespace RtVideo

// ==== B3R W0-P0c: Runtime bodies reached by retail address ====
// ENDGAME B.3R (B3R_RULES R5): each body below is registered at the retail
// address in its trailing comment and read through the registry today. It is
// declared here, copied from its definition (linkage, namespace, types), so the
// reader can call it by name. Nothing calls it through this declaration yet.

// ---- video/context_lifecycle.cpp
extern "C" void RtVideo_Context_Dtor(Video::Context* self);  // 0x1800f16e0
extern "C" void Rt_GuardedObjectFree(void* p);  // 0x180006a10
// B3R RT-Core: ??3Context@Video@@SAXPEAX@Z, called by name from ~MainLoop.
extern "C" void Video_Context_OperatorDelete(void* p);  // 0x180018500
// ==== end B3R W0-P0c ====
