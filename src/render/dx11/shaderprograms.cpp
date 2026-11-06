// VideoDX11 VertexProgramData (vtable 0x180018890) and PixelProgramData
// (vtable 0x180018800) — all six slots.
//
// The two classes are the same class twice. Byte for byte, the only differences
// between the vertex and pixel versions of each of the three functions are the
// vtable address stored by the destructor, the Runtime base destructor it
// chains to, and which of ID3D11Device::CreateVertexShader /
// ID3D11DeviceContext::VSSetShader it calls versus their pixel counterparts.
// They are ported together here so the shared reasoning is written once.
//
//   slot  vertex       pixel        role
//   ----  -----------  -----------  ------------------------------------------
//     0   0x180004110  0x180003580  scalar deleting destructor
//     1   0x1800041a0  0x180003610  PlatformCreate — compile the permutations
//     2   0x180004230  0x1800036a0  Bind(Context&, int permutation)
//
// Slot names come from the Runtime export table, which names the facade methods
// these virtuals implement:
//     ?PlatformCreate@VertexProgram@Video@@AEAA_NXZ        -> bool, no args
//     ?PlatformCreate@PixelProgram@Video@@AEAA_NXZ         -> bool, no args
//     ?Bind@VertexProgram@Video@@QEBAXAEAVContext@2@H@Z    -> (Context&, int)
//     ?Bind@PixelProgram@Video@@QEBAXAEAVContext@2@H@Z     -> (Context&, int)
//
// The shape of Bind is worth stating explicitly because it decides the ABI: the
// Context& lands in RDX and the permutation index in R8D. Both 0x180004230 and
// 0x1800036a0 clobber RDX with the Video::Device singleton before ever reading
// it, so the DX11 backend genuinely ignores the context it is handed and uses
// the global instead. Our signatures keep the parameter (it must be consumed
// off the register) and mark it unused.
//
// ---------------------------------------------------------------------------
// The permutation array
// ---------------------------------------------------------------------------
// Both classes are 0x30 bytes: the 0x10-byte Runtime base plus a 4-entry array
// of shader objects at +0x10. The owner (Video::VertexProgram /
// Video::PixelProgram, both deriving from Video::ShaderProgram) carries a
// Collection::Array<Collection::Array<unsigned char>> of compiled byte-code
// blobs at +0x40 — one per permutation. PlatformCreate creates a D3D11 shader
// object for each blob the program actually has, up to a hard limit of 4;
// entries past the program's own count stay null.
//
// Bind then does the fallback that makes the whole scheme work:
//
//     i = variants[requested] ? requested : 0;
//     VSSetShader(variants[i], nullptr, 0);
//
// i.e. permutation 0 is the "always compiled" one, and asking for a permutation
// that was never built silently falls back to it rather than binding null.
//
// There is NO bounds check on `requested` in the original — the index is
// sign-extended from int32 and used directly. We reproduce that. Clamping it
// would hide a caller bug rather than surface it, and would be a behavioural
// divergence in exactly the case that matters.

#include "dx11_backend_access.h"

// The real `Video::VertexProgramData` / `PixelProgramData` abstract bases. B5
// RES: the two `VideoDX11::` classes derive from them in C++
// (src/render/dx11/VideoDX11Data.h), so the compiler chains their destructors and
// builds their vtables.
#include "video/CompanionData.h"
#include "better_trees/foliage_aa_stage.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/BETTER-TREES.md, FAA-CORE.md): ext.better_trees' foliage_aa stage, VideoDX11::g_extFoliageAA
#include "better_trees/billboard_shadow_stage.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/BETTER-TREES.md, TS-CORE.md): ext.better_trees' billboard_shadow stage, VideoDX11::g_extBillboardShadow
#include "draw_batch/smoke_vs_stage.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/DRAWBATCH.md): ext.draw_batch's particle VS stage, VideoDX11::g_extDrawBatchVs
#include "decal_fog/decal_fog_backend.h"   // EXT SEAM (NOT RETAIL, docs/retail_bugs.md section 32): fix.decal_fog_order's corrected decal programs

#include <cstdio>
#include <vector>

namespace VideoDX11 {

// ============================================================
// VertexProgramData
// ============================================================

// 0x180004110 — scalar deleting destructor.
//
//     180004137  LEA  RAX,[0x180018890]      ; DX11 vtable
//     18000413e  MOV  qword ptr [RCX],RAX    ; re-seat vptr FIRST
//     180004150  loop x4: if (variants[i]) variants[i]->Release()
//     18000416a  CALL [0x180013318]          ; Video::VertexProgramData::~VertexProgramData
//     180004179  CALL [0x1800134d0]          ; MemFree, only when (flags & 1)
//     18000417f  MOV  RAX,RSI                ; returns `this`
//
// Order matters and is preserved: vptr store, then the four Releases, then the
// Runtime base destructor, then the free. The original does not null the
// variant pointers after releasing them; neither do we — the storage is about
// to be freed or the object re-created, and writing them would be an extra
// store the original never made.
//
// B5 RES: the vptr store, the base-destructor chain and the conditional MemFree
// are the compiler's (its D0 + the base's class-scope `operator delete`, B5 W0,
// companion_operators.cpp). The body is the four Releases, in index order.
VertexProgramData::~VertexProgramData() {
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/BETTER-TREES.md, TS-CORE.md): ext.better_trees' billboard_shadow stage, null unless the row is on; the stage drops its copies.
    if (BbsStage::Stage* bbs = g_extBillboardShadow.load(std::memory_order_acquire)) bbs->VertexDestroyed(this);
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/DRAWBATCH.md): ext.draw_batch's particle VS stage, null unless the row is on; the stage drops its copies.
    if (DbStage::Stage* db = g_extDrawBatchVs.load(std::memory_order_acquire)) db->VertexDestroyed(this);
    for (int i = 0; i < kShaderVariantCount; ++i)
        if (variants[i]) variants[i]->Release();
}

// 0x1800041a0 — VertexProgram::PlatformCreate. Returns bool.
//
//     1800041cd  MOV R14,[deviceData+0x20]        ; ID3D11Device*, fetched ONCE
//     1800041d5  CMP EBX,[owner+0x48]             ; i < permutation count?
//     1800041d8  JGE  skip                        ;   no -> leave variants[i] null
//     1800041da  MOV RAX,[owner+0x40]             ; blob array base
//     1800041e4  MOVSXD R8,[RAX+RDI+8]            ; blob[i].length (int32, sign-extended)
//     1800041e9  MOV RDX,[RAX+RDI]                ; blob[i].data
//     1800041e1  XOR R9D,R9D                      ; pClassLinkage = nullptr
//     1800041f4  LEA RCX,[RSI+(i+2)*8]            ; &this->variants[i]  (this+0x10+i*8)
//     180004200  CALL [R14vt+0x60]                ; index 12 = CreateVertexShader
//     180004204  TEST EAX,EAX / JNZ fail          ; ANY non-zero HRESULT aborts
//     180004213  MOV AL,1 / RET                   ; all four handled -> true
//
// Three details that are easy to get wrong and are deliberate here:
//
//  * The loop always runs 4 iterations. Permutations the program does not have
//    are skipped, not break-ed out of, so a sparse byte-code array would still
//    fill the later slots. (In practice `length` is the prefix count, but the
//    original's control flow is a skip, and we match it.)
//  * The failure test is `hr != 0`, not FAILED(hr). A positive non-S_OK
//    HRESULT would abort in the original, so it aborts here.
//  * On failure it returns false immediately, leaving the already-created
//    variants in place and un-released. The destructor cleans them up.
//
// ID3D11Device vtable index 12 == byte offset 0x60 == CreateVertexShader:
// ID3D11Device inherits IUnknown (0..2) then declares CreateBuffer(3),
// CreateTexture1D/2D/3D(4,5,6), CreateShaderResourceView(7),
// CreateUnorderedAccessView(8), CreateRenderTargetView(9),
// CreateDepthStencilView(10), CreateInputLayout(11), CreateVertexShader(12).
// Independently corroborated inside this DLL: 0x1800052b0/0x180005350 call
// [device+0x28] == index 5 == CreateTexture2D, and 0x180002870 calls
// [device+0x48] == index 9 == CreateRenderTargetView and [device+0x50] ==
// index 10 == CreateDepthStencilView, both of which match this numbering.
bool VertexProgramData::PlatformCreate() {
    ID3D11Device* device = Dx11Device();

    for (int i = 0; i < kShaderVariantCount; ++i) {
        // ENDGAME B.3.5 block B: was `const ShaderProgramOwner*`, a partial
        // view in dx11_backend_access.h. `Video::ShaderProgram` is a real class
        // now and this is the same two offsets. The owner is a
        // `Video::VertexProgram`; the fields read here are its ShaderProgram
        // base's, which is what made the view's offsets base-class offsets.
        // B5 RES: the base's `_owner` is typed `Video::VertexProgram*`.
        const Video::ShaderProgram* owner = _owner;
        if (i >= owner->_byteCode.GetSize()) continue;

        // ENDGAME B.3.2 step 2 — `byteCode` is the real
        // `Collection::Array<Collection::Array<uint8_t>>`; the element read that
        // needed a cast off a `void*` is now `operator[]`.
        const RtByteCodeBlob* blobs = owner->_byteCode.GetData();
        HRESULT hr = device->CreateVertexShader(
            blobs[i].GetData(),
            static_cast<SIZE_T>(blobs[i].GetSize()),
            nullptr,
            &variants[i]);
        if (hr != 0) return false;
    }
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/BETTER-TREES.md, TS-CORE.md): ext.better_trees' billboard_shadow stage, null unless the row is on; the stage makes the copies.
    if (BbsStage::Stage* bbs = g_extBillboardShadow.load(std::memory_order_acquire)) bbs->VertexCreated(this);
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/DRAWBATCH.md): ext.draw_batch's particle VS stage, null unless the row is on; the stage makes the copies.
    if (DbStage::Stage* db = g_extDrawBatchVs.load(std::memory_order_acquire)) db->VertexCreated(this);
    return true;
}

// 0x180004230 — VertexProgram::Bind(Context&, int permutation).
//
//     4c8bd1        MOV   R10,RCX                    ; this
//     4963c0        MOVSXD RAX,R8D                   ; permutation
//     33c9          XOR   ECX,ECX
//     49394cc210    CMP   [R10+RAX*8+0x10],RCX       ; variants[permutation] == 0 ?
//     440f44c1      CMOVZ R8D,ECX                    ;   yes -> fall back to 0
//     4533c9        XOR   R9D,R9D                    ; NumClassInstances = 0
//     4533c0        XOR   R8D,R8D                    ; ppClassInstances  = nullptr
//     498b54d210    MOV   RDX,[R10+RDX*8+0x10]       ; variants[i]
//     48ff6058      JMP   [ctxvt+0x58]               ; index 11 = VSSetShader
//
// ID3D11DeviceContext vtable index 11 == byte offset 0x58 == VSSetShader:
// ID3D11DeviceChild contributes 0..6 (IUnknown 0..2, GetDevice 3,
// Get/Set/SetInterface PrivateData 4..6), then VSSetConstantBuffers(7),
// PSSetShaderResources(8), PSSetShader(9), PSSetSamplers(10), VSSetShader(11).
// Corroborated within this DLL: IASetInputLayout is index 17 == 0x88 in
// 0x180003fc0, IASetIndexBuffer index 19 == 0x98 in 0x180003380,
// OMSetRenderTargets index 33 == 0x108 in 0x180001070 — all consistent.
void VertexProgramData::Bind(void* /*Video::Context& — ignored, see header*/,
                             int permutation) {
    ID3D11DeviceContext* ctx = Dx11Context();
    const int i = variants[permutation] ? permutation : 0;
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/DRAWBATCH.md): ext.draw_batch's particle VS stage, null unless the row is on.
    // Set, the stage binds this variant's rewritten copy when it has one (a particle program) and returns true.
    if (DbStage::Stage* db = g_extDrawBatchVs.load(std::memory_order_acquire)) {
        if (db->BindVertex(ctx, this, i)) return;
    }
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/BETTER-TREES.md, TS-CORE.md): ext.better_trees' billboard_shadow stage, null unless the row is on -- the faithful path's one
    // test per bind. Set, the stage binds this variant or its rewritten copy (TREE_SHADOW.md §4.2).
    if (BbsStage::Stage* bbs = g_extBillboardShadow.load(std::memory_order_acquire)) {
        bbs->BindVertex(ctx, this, i);
        return;
    }
    ctx->VSSetShader(variants[i], nullptr, 0);
}

// ============================================================
// PixelProgramData — identical logic, pixel-stage calls
// ============================================================

// 0x180003580 — scalar deleting destructor.
//   1800035a7 LEA RAX,[0x180018800] / 1800035da CALL [0x180013358]
//   (Video::PixelProgramData::~PixelProgramData) / 1800035e9 CALL [0x1800134d0]
//   B5 RES: as ~VertexProgramData above -- the four Releases; the vptr store,
//   the base chain and the MemFree are the compiler's.
PixelProgramData::~PixelProgramData() {
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/BETTER-TREES.md, FAA-CORE.md): ext.better_trees' foliage_aa stage, null unless the row is on; the stage drops its copies.
    if (FaaStage::Stage* faa = g_extFoliageAA.load(std::memory_order_acquire)) faa->PixelDestroyed(this);
    for (int i = 0; i < kShaderVariantCount; ++i)
        if (variants[i]) variants[i]->Release();
}

// 0x180003610 — PixelProgram::PlatformCreate. Byte-identical to 0x1800041a0
// except for the device vtable offset: 0x78 == index 15 == CreatePixelShader
// (CreateVertexShader 12, CreateGeometryShader 13,
// CreateGeometryShaderWithStreamOutput 14, CreatePixelShader 15).
bool PixelProgramData::PlatformCreate() {
    ID3D11Device* device = Dx11Device();

    for (int i = 0; i < kShaderVariantCount; ++i) {
        // B.3.5 block B / B5 RES, as in VertexProgramData::PlatformCreate above.
        const Video::ShaderProgram* owner = _owner;
        if (i >= owner->_byteCode.GetSize()) continue;

        // ENDGAME B.3.2 step 2 — `byteCode` is the real
        // `Collection::Array<Collection::Array<uint8_t>>`; the element read that
        // needed a cast off a `void*` is now `operator[]`.
        const RtByteCodeBlob* blobs = owner->_byteCode.GetData();
        // EXT SEAM (NOT RETAIL, docs/retail_bugs.md section 32): fix.decal_fog_order -- a decal program's
        // corrected copy (tint, then fog) when the row is on; retail's bytes otherwise, or when the copy
        // is refused by the device.
        std::vector<uint8_t> fixedCopy;
        if (DecalFogBackend::FixedCopy(blobs[i].GetData(), static_cast<size_t>(blobs[i].GetSize()), &fixedCopy)) {
            const HRESULT fixedHr = device->CreatePixelShader(fixedCopy.data(), fixedCopy.size(), nullptr, &variants[i]);
            if (fixedHr == 0) continue;
            std::fprintf(stderr, "[DecalFog] CreatePixelShader of the corrected copy failed hr=0x%08lx: kept retail\n",
                         static_cast<unsigned long>(fixedHr));
            variants[i] = nullptr;
        }
        HRESULT hr = device->CreatePixelShader(
            blobs[i].GetData(),
            static_cast<SIZE_T>(blobs[i].GetSize()),
            nullptr,
            &variants[i]);
        if (hr != 0) return false;
    }
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/BETTER-TREES.md, FAA-CORE.md): ext.better_trees' foliage_aa stage, null unless the row is on; the stage makes the copies.
    if (FaaStage::Stage* faa = g_extFoliageAA.load(std::memory_order_acquire)) faa->PixelCreated(this);
    return true;
}

// 0x1800036a0 — PixelProgram::Bind(Context&, int permutation).
// Same instruction sequence as 0x180004230 with the tail-jmp through
// [ctxvt+0x48] == index 9 == PSSetShader.
void PixelProgramData::Bind(void* /*Video::Context& — ignored*/,
                            int permutation) {
    ID3D11DeviceContext* ctx = Dx11Context();
    const int i = variants[permutation] ? permutation : 0;
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/BETTER-TREES.md, FAA-CORE.md): ext.better_trees' foliage_aa stage, null unless the row is on -- the faithful path's one
    // test per bind. Set, the stage binds this variant or its transformed copy (+ the A2C clone).
    if (FaaStage::Stage* faa = g_extFoliageAA.load(std::memory_order_acquire)) {
        faa->BindPixel(ctx, this, i);
        return;
    }
    ctx->PSSetShader(variants[i], nullptr, 0);
}

}  // namespace VideoDX11

// All live. Verified in-game and RenderDoc-diffed against the shipped DLL —
// see src/render/dx11/README.md §"Per-draw pipeline-state equality".
