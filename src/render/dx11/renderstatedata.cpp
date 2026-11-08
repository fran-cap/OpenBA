// VideoDX11 RenderStateData — all 3 vtable slots (vtable 0x180018818).
//
// This is the class that turns the engine's render-state *description*
// (Video::RenderState, a resource loaded from .rsc data) into the three
// immutable D3D11 state objects, and binds them.
//
//   slot 0  0x1800036e0  scalar deleting dtor  — Release x3, base dtor, MemFree
//   slot 1  0x180003760  Create                — the three CreateXxxState calls
//   slot 2  0x180003a90  Bind                  — OMSetBlendState / OMSetDepthStencilState / RSSetState
//
// Who calls what (verified in Runtime-x64.dll, so the slot roles are not guesses):
//
//   Video::RenderState::PlatformCreate   0x1800f4ea0  -> data->vtable[1]()
//   Video::RenderState::Bind(Context&)   0x1800f4ec0  -> data->vtable[2]()
//   Video::RenderState::PlatformDestruct 0x1800f4e80  -> data->vtable[0](data, 1)
//
// Note that Runtime's Bind takes a `Video::Context&` and passes it in RDX, but
// the DX11 slot 2 never reads RDX: it walks to the *global* immediate context
// instead (see below). Our signature therefore takes and ignores the argument.
//
// ---------------------------------------------------------------------------
// Reaching D3D11
// ---------------------------------------------------------------------------
// RenderStateData caches neither the device nor the context. Both slots 1 and 2
// walk the same chain on every call, exactly as every other resource Bind in
// this DLL does (0x180003a1c and 0x180003a9a in the disassembly):
//
//     mov rax, [0x1800134b8]      ; import ?_singleton@Device@Video@@0PEAV12@EA
//     mov rcx, [rax]              ; Video::Device*
//     mov rax, [rcx+0x100]        ; Video::DeviceData*   (the DX11 one)
//     mov rbx, [rax+0x20]         ; ID3D11Device*        (slot 1)
//     mov rbx, [rax+0x28]         ; ID3D11DeviceContext* (slot 2)
//
// 0x1800134b8 is VideoDX11's import slot for Runtime's private static
// `Video::Device* Video::Device::_singleton`, which lives at Runtime
// 0x1801fe7a8 — i.e. it is a Video::Device**, hence the two loads. DeviceData
// +0x20 / +0x28 are already pinned by src/render/dx11/VideoDX11Data.h.
//
// ---------------------------------------------------------------------------
// COM vtable offsets — every one verified against the decompilation
// ---------------------------------------------------------------------------
// We call through <d3d11.h>'s C++ interfaces rather than raw vtable indices.
// That is only safe if GCC lays those interfaces out at the offsets the
// original hard-codes, so each one was cross-checked: the index below is the
// position in mingw-w64's ID3D11DeviceVtbl / ID3D11DeviceContextVtbl (counted
// from QueryInterface = 0), and index*8 is the byte offset in the original.
//
//   ID3D11Device        20  CreateBlendState         -> 0xa0   call at 0x180003a3f
//   ID3D11Device        21  CreateDepthStencilState  -> 0xa8   call at 0x180003a68
//   ID3D11Device        22  CreateRasterizerState    -> 0xb0   call at 0x180003a53
//   ID3D11DeviceContext 35  OMSetBlendState          -> 0x118  call at 0x180003ad9
//   ID3D11DeviceContext 36  OMSetDepthStencilState   -> 0x120  call at 0x180003af2
//   ID3D11DeviceContext 43  RSSetState               -> 0x158  call at 0x180003b03
//   IUnknown             2  Release                  -> 0x10   calls at 0x180003713/722/731
//
// NOTE the task brief listed CreateDepthStencilState/BlendState/RasterizerState
// at 0x50/0x58/0x60. That is wrong — those are D3D11-only method indices 10/11/12
// counted past IUnknown, not full-vtable offsets, and it also has Blend and
// DepthStencil the wrong way round. The offsets above are the ones actually
// encoded in the DLL, and they are self-consistent: the object CreateBlendState
// writes to (+0x10) is the one OMSetBlendState later reads, and likewise for
// +0x18 / OMSetDepthStencilState and +0x20 / RSSetState.
//
// ---------------------------------------------------------------------------
// Deliberate deviations from the original (all unobservable to D3D11)
// ---------------------------------------------------------------------------
//  * The original leaves the two padding bytes of D3D11_DEPTH_STENCIL_DESC (the
//    hole after StencilWriteMask, offsets +0x12/+0x13) uninitialised — it stores
//    only a WORD 0xFFFF at +0x10. We value-initialise the whole struct. D3D11
//    does not read those bytes.
//  * The original never checks the HRESULT of the three Create calls and returns
//    `true` unconditionally (`mov al,1` at 0x180003a76). We do the same, so a
//    failed state creation leaves a null pointer that Bind will hand to D3D11 —
//    which is legal and means "default state". Reproduced deliberately.
//  * The original does not release a previously-created state object if Create
//    runs twice. Reproduced (Runtime pairs Create with PlatformDestruct).
//  * The original does not bounds-check any of the six enum lookups. Neither do
//    we: clamping would silently diverge on malformed .rsc data.

#include "better_trees/foliage_aa_stage.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/BETTER-TREES.md, FAA-CORE.md): ext.better_trees' foliage_aa stage, VideoDX11::g_extFoliageAA
#include <cstring>
#include <cstdint>
#include <cstddef>

#include <d3d11.h>

#include "dx11_backend_access.h"   // Dx11DeviceData / Dx11Device / Dx11Context
#include "VideoDX11Data.h"

// The real `Video::RenderStateData` abstract base. B5 RES: `VideoDX11::
// RenderStateData` derives from it in C++ (src/render/dx11/VideoDX11Data.h), so the
// compiler chains its destructor and builds its vtable.
#include "video/CompanionData.h"
#include "video/Device.h"        // ENDGAME B.3.9 VID: Video::Device::_singleton
// B5 RES: the real `Video::RenderState`, whose named members replace this file's
// former `VideoRenderState` mirror (B5_RULES D8 in spirit: no second view of a
// Runtime class).
#include "video/RenderState.h"
#include "depth_bias.h"   // src/render/common: the bias MULSS + CVTTSS2SI (0x180003827-0x18000384b)

namespace VideoDX11 {
namespace {

// ============================================================================
// Video::RenderState — the description object at RenderStateData+0x08
//
// B5 RES: THE `VideoRenderState` MIRROR IS GONE. It was a 0x80-byte private
// copy of `Video::RenderState` (src/engine/video/RenderState.h), which is a
// real class with the same offsets. The slots below read the real class's
// members through the base's typed `_owner`. The mirror's field names were
// the D3D11-consumer names, and for the Stencil sub-struct they disagreed with
// the real header's serializer-order guesses; the header now carries the
// consumer names (its Stencil banner cites the reads below). The map, as
// the slots read it (offsets on the RenderState):
//
//   +0x20 _alphaBlend.enabled    +0x24 .srcBlend   +0x28 .dstBlend
//   +0x2c _alphaBlend.blendOp    +0x30 .writeMask
//   +0x34 _depth.test            +0x35 .write      +0x38 .func
//   +0x3c _depth.bias            +0x40 .slopeBias
//   +0x44 _stencil.enabled       +0x45 .twoSided   +0x48 .func (both faces)
//   +0x4c _stencil.reference (byte)
//   +0x50/+0x54/+0x58 _stencil.failOp / depthFailOp / passOp       (front)
//   +0x5c/+0x60/+0x64 _stencil.backFailOp / backDepthFailOp / backPassOp
//   +0x68 _cullMode   +0x6c _fillMode   +0x70 _scissorEnable
//
// Layout proven by three independent Runtime functions rather than inferred
// from the DX11 reads alone:
//
//   Video::RenderState::RenderState()  0x1800f4b60   default values
//   Video::RenderState::Serialize()    0x1800f4c90   field order + widths
//   Video::RenderState::Stencil::Serialize() 0x1800f48e0
//   Video::RenderState::Depth::Serialize()   0x1800f4a00
//   Video::RenderState::AlphaBlend::Serialize() 0x1800f4aa0
//
// The public API is
//   RenderState::Initialize(const AlphaBlend&, const Depth&, const Stencil&,
//                           CullMode, FillMode, bool)
// (mangled name at Runtime 0x1801dfe97), which is exactly the five groups
// above, in order.
//
// Serializer virtual used per field tells the width: +0x78 = bool,
// +0x80 = float, +0x98 = int/enum, +0xb8 = byte.
// ============================================================================

// ============================================================================
// The six engine-enum -> D3D11-enum translation tables.
//
// Each is a literal transcription of a stack table the original builds at the
// top of 0x180003760, cited by the instruction that writes it. The *values* are
// therefore exact; the enumerator *names* below are inferred from the D3D11
// constant each slot maps to (and corroborated by RenderState's ctor defaults
// and by Video::RenderState::IsWireframe, 0x18004b730, which is literally
// `return fillMode == 1`).
// ============================================================================

// T1 @ [RSP+0x20..0x44], written at 0x180003783-0x1800037cb. 10 entries.
// D3D11_BLEND. Engine order pairs each factor with its inverse.
//   0 Zero  1 One  2 SrcColor  3 InvSrcColor  4 SrcAlpha  5 InvSrcAlpha
//   6 DestColor  7 InvDestColor  8 DestAlpha  9 InvDestAlpha
static const D3D11_BLEND kBlendFactor[10] = {
    D3D11_BLEND_ZERO,            //  0 -> 1
    D3D11_BLEND_ONE,             //  1 -> 2
    D3D11_BLEND_SRC_COLOR,       //  2 -> 3
    D3D11_BLEND_INV_SRC_COLOR,   //  3 -> 4
    D3D11_BLEND_SRC_ALPHA,       //  4 -> 5
    D3D11_BLEND_INV_SRC_ALPHA,   //  5 -> 6
    D3D11_BLEND_DEST_COLOR,      //  6 -> 9
    D3D11_BLEND_INV_DEST_COLOR,  //  7 -> 10
    D3D11_BLEND_DEST_ALPHA,      //  8 -> 7
    D3D11_BLEND_INV_DEST_ALPHA,  //  9 -> 8
};

// T3b @ [RSP+0x78..0x80], written at 0x18000382c/0x180003834/0x18000383c.
// 3 entries. D3D11_BLEND_OP. Min/Max are not reachable from the engine enum.
static const D3D11_BLEND_OP kBlendOp[3] = {
    D3D11_BLEND_OP_ADD,           // 0 -> 1
    D3D11_BLEND_OP_SUBTRACT,      // 1 -> 2
    D3D11_BLEND_OP_REV_SUBTRACT,  // 2 -> 3
};

// T2 @ [RBP-0x78..-0x5c], written at 0x180003857-0x1800038ae. 8 entries.
// D3D11_COMPARISON_FUNC. Used for BOTH depthFunc and stencilFunc.
static const D3D11_COMPARISON_FUNC kCompareFunc[8] = {
    D3D11_COMPARISON_NEVER,          // 0 -> 1
    D3D11_COMPARISON_ALWAYS,         // 1 -> 8
    D3D11_COMPARISON_EQUAL,          // 2 -> 3
    D3D11_COMPARISON_LESS,           // 3 -> 2
    D3D11_COMPARISON_LESS_EQUAL,     // 4 -> 4   (RenderState ctor default)
    D3D11_COMPARISON_GREATER,        // 5 -> 5
    D3D11_COMPARISON_GREATER_EQUAL,  // 6 -> 7
    D3D11_COMPARISON_NOT_EQUAL,      // 7 -> 6
};

// T3 @ [RSP+0x58..0x74], written at 0x1800038b8-0x1800038f0. 8 entries.
// D3D11_STENCIL_OP. Note 2/3 are the *wrapping* INCR/DECR and 4/5 the
// saturating ones — easy to get backwards, so: 2->7 (INCR), 3->8 (DECR),
// 4->4 (INCR_SAT), 5->5 (DECR_SAT).
static const D3D11_STENCIL_OP kStencilOp[8] = {
    D3D11_STENCIL_OP_KEEP,      // 0 -> 1
    D3D11_STENCIL_OP_ZERO,      // 1 -> 2
    D3D11_STENCIL_OP_INCR,      // 2 -> 7
    D3D11_STENCIL_OP_DECR,      // 3 -> 8
    D3D11_STENCIL_OP_INCR_SAT,  // 4 -> 4
    D3D11_STENCIL_OP_DECR_SAT,  // 5 -> 5
    D3D11_STENCIL_OP_REPLACE,   // 6 -> 3
    D3D11_STENCIL_OP_INVERT,    // 7 -> 6
};

// T2b @ [RBP-0x58..-0x50], written at 0x1800037f7/0x180003808/0x18000380f.
// 3 entries. D3D11_CULL_MODE. The engine's ordering is Front/Back/None — NOT
// the more usual None-first — but the ctor default of 1 mapping to CULL_BACK
// corroborates it.
static const D3D11_CULL_MODE kCullMode[3] = {
    D3D11_CULL_FRONT,  // 0 -> 2
    D3D11_CULL_BACK,   // 1 -> 3   (RenderState ctor default)
    D3D11_CULL_NONE,   // 2 -> 1
};

// T4 @ [RBP+0x100..0x104] (the caller's shadow space!), written at
// 0x1800037dc/0x1800037e6. 2 entries. D3D11_FILL_MODE.
static const D3D11_FILL_MODE kFillMode[2] = {
    D3D11_FILL_SOLID,      // 0 -> 3   (RenderState ctor default)
    D3D11_FILL_WIREFRAME,  // 1 -> 2   (== Video::RenderState::IsWireframe)
};

// The fields index these tables through RenderState::TableIndex (review S-042), with its counts.
static_assert(sizeof kBlendFactor / sizeof kBlendFactor[0] == ::Video::RenderState::kBlendFactorCount,
              "kBlendFactor");
static_assert(sizeof kBlendOp / sizeof kBlendOp[0] == ::Video::RenderState::kBlendOpCount,
              "kBlendOp");
static_assert(sizeof kCompareFunc / sizeof kCompareFunc[0] == ::Video::RenderState::kCompareFuncCount,
              "kCompareFunc");
static_assert(sizeof kStencilOp / sizeof kStencilOp[0] == ::Video::RenderState::kStencilOpCount,
              "kStencilOp");
static_assert(sizeof kCullMode / sizeof kCullMode[0] == ::Video::RenderState::kCullModeCount,
              "kCullMode");
static_assert(sizeof kFillMode / sizeof kFillMode[0] == ::Video::RenderState::kFillModeCount,
              "kFillMode");

// Depth-bias scale factors, read as float constants by the original:
//   0x180018c5c = 0x4B7FFFFF = 16777215.0f  (2^24 - 1: the D24 depth range,
//                                            matching the D24_UNORM_S8_UINT
//                                            depth buffer this backend creates)
//   0x180018c58 = 0x45800000 = 4096.0f
// The first is Render::kDepthBiasScale (MULSS @ 0x180003843), applied by Render::DepthBiasUnits.
static const float kSlopeScaledBiasScale = 4096.0f;      // MULSS @ 0x18000387d

// The D3D11 desc structs must be exactly the shapes the original hard-codes.
// The blend-desc size in particular is load-bearing: the original memsets
// 0x108 bytes at 0x180003821 (`mov r8d, 0x108`).
static_assert(sizeof(D3D11_BLEND_DESC)         == 0x108, "D3D11_BLEND_DESC (memset 0x108 @ 0x180003821)");
static_assert(sizeof(D3D11_RASTERIZER_DESC)    == 0x28,  "D3D11_RASTERIZER_DESC");
static_assert(sizeof(D3D11_DEPTH_STENCIL_DESC) == 0x34,  "D3D11_DEPTH_STENCIL_DESC");
static_assert(offsetof(D3D11_BLEND_DESC, RenderTarget)              == 0x08, "BLEND_DESC::RenderTarget");
static_assert(offsetof(D3D11_DEPTH_STENCIL_DESC, StencilReadMask)   == 0x10, "DS_DESC::StencilReadMask");
static_assert(offsetof(D3D11_DEPTH_STENCIL_DESC, FrontFace)         == 0x14, "DS_DESC::FrontFace");
static_assert(offsetof(D3D11_DEPTH_STENCIL_DESC, BackFace)          == 0x24, "DS_DESC::BackFace");
static_assert(offsetof(D3D11_RASTERIZER_DESC, DepthBias)            == 0x0c, "RS_DESC::DepthBias");
static_assert(offsetof(D3D11_RASTERIZER_DESC, SlopeScaledDepthBias) == 0x14, "RS_DESC::SlopeScaledDepthBias");
static_assert(offsetof(D3D11_RASTERIZER_DESC, MultisampleEnable)    == 0x20, "RS_DESC::MultisampleEnable");

// ============================================================================
// Runtime-side dependencies — DIRECT REFERENCES (ENDGAME B.3R, block DX)
//
// This was `RuntimeDeps`: a function-local static holding the address of
// Video::Device::_singleton and a `?MemFree@MemoryAllocator@System@@SAXPEAX@Z`
// pointer (VideoDX11 import slot 0x1800134d0) resolved by `ResolveExport`, with
// a readiness flag that sent slot 0 to `Dx11OriginalSlot` when it failed. The
// name bound to a body in this build and the address of a class static is never
// null, so the flag was always true; the struct, the flag and slot 0's
// forwarding arm are GONE (B3R_RULES R2, R8). Slot 0 calls
// `System::MemoryAllocator::MemFree`, and the chain below reads the class
// static LIVE, as before (it was never cached, only its address was).
//
// ENDGAME B.3.5 block C, RULE B5-3: the second import,
// `[0x180013348] -> ??1RenderStateData@Video@@UEAA@XZ` (hint 0x295), went
// earlier; since B5 RES the base dtor is the compiler's chain (slot 0).
// ============================================================================

// The chain every slot walks. Returns null if the device has not been created
// yet — the original would fault here. The two slots below test for that and
// take a documented early return (B5_RULES D9: the old "forward to the original
// slot" arm is deleted; with no shipped DLL it returned null into a call).
// B5 RES: the Device+0x100 read is `Dx11DeviceData()` (dx11_backend_access.h),
// the same two loads.
DeviceData* ActiveDeviceData() {
    if (!::Video::Device::_singleton) return nullptr;   // Video::Device*
    return Dx11DeviceData();                            // Video::Device+0x100
}

}  // namespace

// ============================================================================
// slot 0 — 0x1800036e0
// `RenderStateData* ~RenderStateData(RenderStateData* this, unsigned flags)`
//
// The standard MSVC scalar deleting destructor. Exact original sequence:
//   1. store our vptr        (lea rax,[0x180018818]; mov [rcx],rax)
//   2. Release +0x10 if non-null   (blend)
//   3. Release +0x18 if non-null   (depth-stencil)
//   4. Release +0x20 if non-null   (rasterizer)
//   5. Video::RenderStateData::~RenderStateData(this)   — the Runtime base
//   6. if (flags & 1) MemFree(this)
//   7. return this
//
// Note the release order is blend, depth-stencil, rasterizer — i.e. field
// order, NOT the order the three were created in (blend, rasterizer,
// depth-stencil). Reproduced as-is. The fields are *not* nulled afterwards;
// Runtime always follows this with the object going away.
//
// B5 RES: steps 1, 5, 6 and 7 are the compiler's -- the vptr store and the
// base-destructor chain are what `~RenderStateData()` does by itself, and the
// conditional MemFree is its D0 calling the base's class-scope `operator delete`
// (B5 W0, src/engine/video/companion_operators.cpp). The body is steps 2-4.
// ============================================================================
RenderStateData::~RenderStateData() {
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/BETTER-TREES.md, FAA-CORE.md): ext.better_trees' foliage_aa stage, null unless the row is on; the stage drops the clone.
    if (FaaStage::Stage* faa = g_extFoliageAA.load(std::memory_order_acquire)) faa->RenderStateDestroyed(blendState);
    if (blendState) {
        blendState->Release();
    }
    if (depthStencilState) {
        depthStencilState->Release();
    }
    if (rasterizerState) {
        rasterizerState->Release();
    }
}

// ============================================================================
// slot 1 — 0x180003760  "Create"  (Video::RenderState::PlatformCreate)
//
// Builds all three D3D11 state objects from the owner Video::RenderState.
// Returns true unconditionally (`mov al,1` @ 0x180003a76) — the three HRESULTs
// are discarded.
//
// Call order is fixed and reproduced: Blend, Rasterizer, DepthStencil.
// ============================================================================
bool RenderStateData::Create() {
    DeviceData* dd = ActiveDeviceData();
    if (!dd || !dd->d3dDevice) {
        // B5_RULES D9: was `Dx11OriginalSlot(Vt::RenderStateData, 1)` called if
        // non-null, else `true`. There is no shipped DLL, so the lookup was
        // always null and this arm always returned true: kept as that return.
        return true;
    }
    ID3D11Device* dev = Dx11Device();   // == dd->d3dDevice, re-walked
    const ::Video::RenderState* rs = _owner;   // B5 RES: the real class, typed
    // NOT RETAIL (UB, out-of-bounds index; review S-042): retail 0x1800037c0.. indexes its
    // stack tables with the raw serialized fields, so a corrupt or mod .rsc value read past a
    // table. In range a field indexes itself (byte-identical); out of range it takes its
    // constructor default (RenderState.h).
    using RS = ::Video::RenderState;
    const uint32_t srcBlend        = RS::TableIndex(rs->_alphaBlend.srcBlend, RS::kBlendFactorCount, 1);
    const uint32_t dstBlend        = RS::TableIndex(rs->_alphaBlend.dstBlend, RS::kBlendFactorCount, 0);
    const uint32_t blendOp         = RS::TableIndex(rs->_alphaBlend.blendOp, RS::kBlendOpCount, 0);
    const uint32_t fillMode        = RS::TableIndex(rs->_fillMode, RS::kFillModeCount, 0);
    const uint32_t cullMode        = RS::TableIndex(rs->_cullMode, RS::kCullModeCount, 1);
    const uint32_t depthFunc       = RS::TableIndex(rs->_depth.func, RS::kCompareFuncCount, 4);
    const uint32_t stencilFunc     = RS::TableIndex(rs->_stencil.func, RS::kCompareFuncCount, 0);
    const uint32_t failOp          = RS::TableIndex(rs->_stencil.failOp, RS::kStencilOpCount, 0);
    const uint32_t depthFailOp     = RS::TableIndex(rs->_stencil.depthFailOp, RS::kStencilOpCount, 0);
    const uint32_t passOp          = RS::TableIndex(rs->_stencil.passOp, RS::kStencilOpCount, 0);
    const uint32_t backFailOp      = RS::TableIndex(rs->_stencil.backFailOp, RS::kStencilOpCount, 0);
    const uint32_t backDepthFailOp = RS::TableIndex(rs->_stencil.backDepthFailOp, RS::kStencilOpCount, 0);
    const uint32_t backPassOp      = RS::TableIndex(rs->_stencil.backPassOp, RS::kStencilOpCount, 0);

    // ---- D3D11_BLEND_DESC ------------------------------------------------
    // memset 0x108 (@0x180003821), then nine explicit fields. Everything the
    // original leaves zero stays zero here: AlphaToCoverageEnable,
    // IndependentBlendEnable, and RenderTarget[1..7] (which are unused because
    // IndependentBlendEnable is FALSE).
    D3D11_BLEND_DESC blendDesc;
    std::memset(&blendDesc, 0, sizeof(blendDesc));
    blendDesc.AlphaToCoverageEnable  = FALSE;                      // [RBP-0x20].lo, 0x180003905
    blendDesc.IndependentBlendEnable = FALSE;                      // [RBP-0x20].hi, 0x180003905
    blendDesc.RenderTarget[0].BlendEnable    = (rs->_alphaBlend.enabled != 0);             // RS+0x20, 0x18000390b
    blendDesc.RenderTarget[0].SrcBlend       = kBlendFactor[srcBlend];                  // RS+0x24, 0x180003914
    blendDesc.RenderTarget[0].DestBlend      = kBlendFactor[dstBlend];                  // RS+0x28, 0x18000391f
    blendDesc.RenderTarget[0].BlendOp        = kBlendOp[blendOp];                       // RS+0x2c, 0x18000392a
    // Alpha blending is NOT separately configurable: the alpha triplet reads
    // the very same three RenderState fields (0x180003935 / 0x180003940 /
    // 0x18000394b re-load RS+0x24, +0x28, +0x2c).
    blendDesc.RenderTarget[0].SrcBlendAlpha  = kBlendFactor[srcBlend];
    blendDesc.RenderTarget[0].DestBlendAlpha = kBlendFactor[dstBlend];
    blendDesc.RenderTarget[0].BlendOpAlpha   = kBlendOp[blendOp];
    // movzx eax,byte [rdi+0x30]; and al,0xf   @ 0x180003956 — low nibble only,
    // i.e. the four D3D11_COLOR_WRITE_ENABLE_{RED,GREEN,BLUE,ALPHA} bits.
    blendDesc.RenderTarget[0].RenderTargetWriteMask =
        static_cast<UINT8>(static_cast<uint8_t>(rs->_alphaBlend.writeMask) & 0x0Fu);

    // ---- D3D11_RASTERIZER_DESC -------------------------------------------
    // Fully written, no memset in the original (all ten fields are assigned).
    D3D11_RASTERIZER_DESC rasterDesc;
    std::memset(&rasterDesc, 0, sizeof(rasterDesc));
    rasterDesc.FillMode              = kFillMode[fillMode];       // RS+0x6c, 0x1800037d3
    rasterDesc.CullMode              = kCullMode[cullMode];       // RS+0x68, 0x180003801
    rasterDesc.FrontCounterClockwise = FALSE;                     // hard 0,   0x180003805
    // cvttss2si of (RS+0x3c * 16777215.0f) — truncation toward zero, matching
    // the C float->int conversion the original compiled to. Render::DepthBiasUnits
    // gives CVTTSS2SI's INT_MIN for an out-of-range product (NOT RETAIL there: the
    // plain cast is undefined).
    rasterDesc.DepthBias             =
        Render::DepthBiasUnits(rs->_depth.bias);                  // 0x180003827-0x18000384b
    rasterDesc.DepthBiasClamp        = 0.0f;                      // xorps,    0x18000385e
    rasterDesc.SlopeScaledDepthBias  =
        rs->_depth.slopeBias * kSlopeScaledBiasScale;             // 0x180003863-0x180003885
    rasterDesc.DepthClipEnable       = TRUE;                      // hard 1,   0x180003868
    // RenderState+0x70 is Initialize's trailing `bool` (`_scissorEnable` in the real
    // header); this read is what makes it the scissor enable.
    rasterDesc.ScissorEnable         = (rs->_scissorEnable != 0);        // RS+0x70,  0x18000388a
    // `mov qword [RBP-0x28], 1` @ 0x1800038f8 covers both trailing BOOLs.
    rasterDesc.MultisampleEnable     = TRUE;
    rasterDesc.AntialiasedLineEnable = FALSE;

    // ---- D3D11_DEPTH_STENCIL_DESC ----------------------------------------
    D3D11_DEPTH_STENCIL_DESC dsDesc;
    std::memset(&dsDesc, 0, sizeof(dsDesc));   // see header note re: padding
    dsDesc.DepthEnable    = (rs->_depth.test != 0);               // RS+0x34, 0x180003961
    // D3D11_DEPTH_WRITE_MASK_ZERO == 0, _ALL == 1, so the raw bool is the enum.
    dsDesc.DepthWriteMask = rs->_depth.write
                                ? D3D11_DEPTH_WRITE_MASK_ALL
                                : D3D11_DEPTH_WRITE_MASK_ZERO;    // RS+0x35, 0x18000396d
    dsDesc.DepthFunc      = kCompareFunc[depthFunc];              // RS+0x38, 0x180003977
    dsDesc.StencilEnable  = (rs->_stencil.enabled != 0);          // RS+0x44, 0x180003983
    // `mov word [RSP+0x30], 0xffff` @ 0x180003986 sets both masks at once.
    dsDesc.StencilReadMask  = 0xFF;
    dsDesc.StencilWriteMask = 0xFF;

    dsDesc.FrontFace.StencilFailOp      = kStencilOp[failOp];                   // RS+0x50
    dsDesc.FrontFace.StencilDepthFailOp = kStencilOp[depthFailOp];              // RS+0x54
    dsDesc.FrontFace.StencilPassOp      = kStencilOp[passOp];                   // RS+0x58
    dsDesc.FrontFace.StencilFunc        = kCompareFunc[stencilFunc];            // RS+0x48

    if (!rs->_stencil.twoSided) {
        // RS+0x45 == 0 -> the fixed back face at 0x1800039ca-0x1800039e2.
        dsDesc.BackFace.StencilFailOp      = D3D11_STENCIL_OP_KEEP;   // 1
        dsDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;   // 1
        dsDesc.BackFace.StencilPassOp      = D3D11_STENCIL_OP_KEEP;   // 1
        dsDesc.BackFace.StencilFunc        = D3D11_COMPARISON_ALWAYS; // 8
    } else {
        dsDesc.BackFace.StencilFailOp      = kStencilOp[backFailOp];                   // RS+0x5c
        dsDesc.BackFace.StencilDepthFailOp = kStencilOp[backDepthFailOp];              // RS+0x60
        dsDesc.BackFace.StencilPassOp      = kStencilOp[backPassOp];                   // RS+0x64
        // NOT a typo and NOT a bug: the back face re-reads RS+0x48
        // (0x180003a10, the same field the front face used). Video::RenderState
        // simply has one stencil compare function — RenderState::Serialize
        // (0x1800f4c90 -> Stencil::Serialize 0x1800f48e0) shows a single int at
        // +0x48 for the whole Stencil sub-struct.
        dsDesc.BackFace.StencilFunc        = kCompareFunc[stencilFunc];
    }

    // ---- create, in the original's order ---------------------------------
    dev->CreateBlendState(&blendDesc, &blendState);
    dev->CreateRasterizerState(&rasterDesc, &rasterizerState);
    dev->CreateDepthStencilState(&dsDesc, &depthStencilState);
    return true;
}

// ============================================================================
// slot 2 — 0x180003a90  "Bind"  (Video::RenderState::Bind)
//
//   OMSetBlendState(blend, {0,0,0,0}, 0xFFFFFFFF)
//   OMSetDepthStencilState(depthStencil, (UINT)(BYTE)renderState[0x4c])
//   RSSetState(rasterizer)
//
// The blend factor is a zeroed float[4] built on the stack at [RSP+0x20]
// (movss of xorps'd XMM0 to +0x20, then a zero qword to +0x24 and a zero dword
// to +0x2c — sixteen zero bytes). The sample mask is `or r9d,-1`.
//
// The stencil reference is a *byte* load (`movzx r8d, byte [rax+0x4c]`), which
// agrees with Runtime serializing that field through the byte virtual (+0xb8 in
// Stencil::Serialize) rather than the int one.
//
// The Video::Context& Runtime passes in RDX is never read.
// ============================================================================
void RenderStateData::Bind(void* /*videoContext — unused, as in the original*/) {
    DeviceData* dd = ActiveDeviceData();
    if (!dd || !dd->d3dContext) {
        // B5_RULES D9: was `Dx11OriginalSlot(Vt::RenderStateData, 2)` called if
        // non-null. There is no shipped DLL, so the lookup was always null and
        // this arm always returned without binding: kept as that return.
        return;
    }
    ID3D11DeviceContext* ctx = Dx11Context();   // == dd->d3dContext, re-walked
    const ::Video::RenderState* rs = _owner;     // B5 RES: the real class, typed

    const FLOAT blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    ctx->OMSetBlendState(blendState, blendFactor, 0xFFFFFFFFu);
    ctx->OMSetDepthStencilState(depthStencilState,
                                static_cast<UINT>(rs->_stencil.reference));   // byte, RS+0x4c
    ctx->RSSetState(rasterizerState);
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/BETTER-TREES.md, FAA-CORE.md): ext.better_trees' foliage_aa stage, null unless the row is on. BindResources binds the
    // pixel program BEFORE the render state, so the A2C clone goes on here, over the retail state.
    if (FaaStage::Stage* faa = g_extFoliageAA.load(std::memory_order_acquire)) faa->AfterRenderState(ctx, blendState);
}

}  // namespace VideoDX11

// All three live. Verified in-game and RenderDoc-diffed against the shipped
// DLL — see src/render/dx11/README.md §"Per-draw pipeline-state equality".
