// ============================================================================
// src/engine/graphics/meshinstance_draw.cpp
// ?Draw@MeshInstance@Graphics@@ (0x180100960) and
// ?Draw@EdgeMeshInstance@Graphics@@ (0x1800a3150).  Vtable batch 6.
// ============================================================================
//
// These are slot 2 of `??_7MeshInstance@Graphics@@6B@` and of
// `??_7EdgeMeshInstance@Graphics@@6B@`, the last unported slot of each table,
// and they are the per-item draw submission for every mesh in the game: pose
// the skeleton, hand the bone matrices and the world transform to the
// Video::Context, bind the vertex/index buffers, and issue the draw.
//
// ---------------------------------------------------------------------------
// WHAT THE PACKED `flags` ARGUMENT CARRIES — different in the two bodies
// ---------------------------------------------------------------------------
// The sixth argument (the first `H` after the float) is a BIT-PACKED word and
// the two classes unpack it differently:
//
//   MeshInstance      node    = (flags >> 12) & 0xf      `SAR EAX,0xc; AND EAX,0xf`
//                     sub     =  flags & 0xfff
//   EdgeMeshInstance  edgePass= (flags >> 15) & 1        `SHR R15D,0xf; AND R15B,1`
//                     sub     =  flags & 0x7fff
//
// MeshInstance's shift is an ARITHMETIC right shift (`SAR`) and
// EdgeMeshInstance's is LOGICAL (`SHR`). On the values these actually carry the
// two agree; the transcription keeps each as it is written, because "this is
// never negative" is a claim about the caller.
//
// ---------------------------------------------------------------------------
// THE 0x18-BYTE DRAW RECORD
// ---------------------------------------------------------------------------
// Both bodies index an array of 0x18-byte records — the same stride
// `Graphics::EdgeMesh::SerializeTriangleListArray` walks six dwords of
// (closure batch 2b, src/engine/graphics/closure_batch2b.cpp). The four fields
// the draw reads are +0x00, +0x04, +0x08, +0x0c, and +0x14 is a SIGNED node
// index whose sign selects which pair of buffers gets bound:
//
//     rec[+0x14] < 0   ->  the "skinned" buffers, stride 0x20 (or 0x10)
//     rec[+0x14] >= 0  ->  the "rigid" buffers,   stride 0x18 (or 0x08)
//
// MeshInstance scales the record index by `sub * 3` and then by 8
// (`LEA R15,[RAX+RAX*2]` then `[R12 + R15*8]`), i.e. `sub * 0x18`. Written that
// way below rather than folded, because the fold is where an off-by-a-factor
// hides.
//
// ---------------------------------------------------------------------------
// HIDDEN RETURN SLOTS
// ---------------------------------------------------------------------------
// Four of the six helpers return a struct by value from a NON-STATIC member, so
// the slot is **RDX** with `this` in RCX, and each returns that slot in RAX
// which the caller feeds straight into the next call. Verified per call site in
// the disassembly, as batch 4 did for `GetSunCamera`:
//     ?Lerp@Quaternion@Math@@QEBA?AV12@AEBV12@AEBVScalar@2@@Z   (this, ret, q, &s)
//     ?AsMatrix@Transform@Math@@QEBA?AVMatrix@2@XZ              (this, ret)
//     ?GetNodeToWorld@MeshInstance@Graphics@@QEBA?AVTransform@Math@@HM@Z
//                                                               (this, ret, n, f)
// `?GetBones@MeshInstance@Graphics@@QEBAHMAEBVTransform@Math@@PEAVMatrix@4@@Z`
// returns an `int` and does NOT have one — its float argument rides in XMM1
// (the SECOND register slot), which is the detail Ghidra's C output loses.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstring>

#include "math/Math.h"
// ENDGAME B.3.5 block E: the real `Graphics::MeshInstance` / `EdgeMeshInstance`.
#include "graphics/Mesh.h"
#include "graphics/GraphicsMesh.h"      // B.4: DetailLevel / TriangleList
#include "graphics/AnimationManager.h"   // B.3.9: AnimationManager::_singleton
#include "video/Context.h"   // ENDGAME B.3.5 caller batch: Video::Context
#include "video/Device.h"    // ENDGAME B.3.9 SS1: Video::Device::_singleton
#include "graphics/skin_memo.h"   // SKINMEMO: the per-Renderer::Draw bone memo

namespace {

// ---------------------------------------------------------------------------
// Video::Context, the fields these two bodies set (B.4: by name, Context.h):
//   `_dirty` (+0x10), `_imageBufferDirty` (+0x18), `_imageBuffers[3]` (+0x48),
//   the vertex binding `_vertexBuffers[0]` / `_vertexOffsets[0]` /
//   `_vertexStrides[0]` (+0xb8 / +0x138 / +0x178), the index binding
//   `_indexBuffer` / `_indexOffset` (+0x1b8 / +0x1c0), `_inverseScale`
//   (+0x3f8, the tuning float4), `_color` (+0x408, whose w is the fade alpha)
//   and `_contextData` (+0x2160), whose slot 12 issues the draw.
// ---------------------------------------------------------------------------

// The three tuning constants and the fallback, read out of the image with
// read_memory rather than converted from decimal:
//   0x180161670 = 0x3b000000    0x180161668 = 0x3a000000
//   0x180161674 = 0x3c008081    0x18016175c = 0xbf800000 (= -1.0f)
constexpr uint32_t kTune0 = 0x3b000000u;
constexpr uint32_t kTune1 = 0x3a000000u;
constexpr uint32_t kTune2 = 0x3c008081u;
constexpr uint32_t kNegOne = 0xbf800000u;

float AsFloat(uint32_t bits) { float f; std::memcpy(&f, &bits, 4); return f; }

// 48 Matrix (0x40 each) — the bone buffer both bodies zero and fill. The count
// is FIXED at 48 in both: retail computes `ECX = arg8 + 0x2e` (MeshInstance) or
// loads the literal 0x2f (EdgeMeshInstance) and runs the zero loop while the
// counter stays non-negative, which is 48 passes of 0x40 bytes. MeshInstance's
// form looks variable and is not — the block it sits in only runs when arg8 is
// 1, which the branch above it has already established. `Matrix()` zeroes.
constexpr int kBoneMatrices = 48;

// ---------------------------------------------------------------------------
// The named dependencies, all members (B.4: the `(self, ret)` wrappers and the
// `void*` receivers are gone):
//   `Math::Quaternion::Lerp`, `Math::Transform::AsMatrix`
//   `Graphics::MeshInstance::GetBones` / `GetNodeToWorld`
//   `Video::Context::SetBones` / `SetTransformScale` (0x18001a400, the
//     four-argument setter -- THE DOOR DECIDED THE OVERLOAD) / `SetColor` /
//     `SetInverseScale` / `SetVertexBuffer` / `DrawIndexed`, each of which
//     retail inlines here and each of which is the ported body of that inline.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// `Math::Transform` — 0x1c: a Quaternion at +0x00 (four floats) and a Vector3
// at +0x10 (three). Both bodies build one on the stack by LERPing the two
// Transforms of an `InterpolatedState<Transform>` (or Geometry's `_transform`
// / `_prevTransform`, laid out the same way):
//
//     out.rotation    = prev.rotation.Lerp(cur.rotation, t)
//     out.translation = prev.translation + (cur.translation - prev.translation) * t
//
// *** `prev` IS THE SECOND OF THE TWO AND `cur` IS THE FIRST. *** Retail puts
// `state + 0x1c` in RCX (the Lerp's `this`) and `state + 0x00` in R8, and the
// translation arithmetic reads `[state+0x10]` as the SUBTRAHEND and
// `[state+0x2c]` as the base. Swapping them runs every animation backwards.
//
// The three translation terms are computed as `(a - b) * t` then `b + that`,
// each in its own register, exactly as retail schedules them. The build has no
// `-march` beyond the x86-64 baseline, so GCC cannot contract these into FMAs
// and the arithmetic is bit-exact against the shipped MULSS/ADDSS pairs.
// ---------------------------------------------------------------------------
Math::Transform LerpTransform(const Math::Transform& cur, const Math::Transform& prev,
                              float t) {
    Math::Transform out;
    // The scalar is passed BY ADDRESS (`AEBVScalar@2@`); retail spills `t` to a
    // stack slot and hands over its address.
    const Math::Quaternion quat = prev.rotation.Lerp(cur.rotation, Math::Scalar(t));

    const float dx = (cur.translation.x - prev.translation.x) * t;
    out.translation.x = prev.translation.x + dx;
    const float dy = (cur.translation.y - prev.translation.y) * t;
    out.translation.y = prev.translation.y + dy;
    const float dz = (cur.translation.z - prev.translation.z) * t;
    out.translation.z = prev.translation.z + dz;
    out.rotation = quat;
    return out;
}

// ---------------------------------------------------------------------------
// The instanced-pose pool slot, retail 0x180100b73 .. 0x180100bbd. IDENTICAL to
// mesh.cpp's `RtInstancedPoolSlot` for `GetNodeToWorld`/`GetInstanceTransform`;
// spelled again here because that one is file-local.
//
//     poolId = _animation._layers[_size - 1]._handle   (layers + size*0x38 - 0x2c)
//     hi = (uint8)(poolId >> 8);   lo = (uint8)poolId;
//     poses = AnimationManager::_singleton->_instances[hi]._states[lo]._nodes._data
//
// B.4: every step is a named field (AnimationState.h / AnimationManager.h).
// ---------------------------------------------------------------------------
const Graphics::InterpolatedState<Math::Transform>*
InstancedPoses(const Graphics::MeshInstance& self) {
    // EXT SEAM (NOT RETAIL, docs/android/P13_OVERLAP.md §2.3): a frozen copy's own record.
    if (::Graphics::MeshInstance::_extInstancedState != nullptr) {
        if (const ::Graphics::InstancedAnimationState* frozen = ::Graphics::MeshInstance::_extInstancedState(self))
            return frozen->_nodes._data;
    }
    const auto& layers = self._animation._layers;
    const int64_t poolId = layers._data[layers._size - 1]._handle;       // MOVSXD

    const uint64_t hi = static_cast<uint8_t>(poolId >> 8);
    const uint64_t lo = static_cast<uint8_t>(poolId);

    return ::Graphics::AnimationManager::_singleton->_instances._data[hi]
               ._states._data[lo]._nodes._data;
}

// The material object bind, `mode != 1` only: `Context::SetImageBuffer(obj, 3)`
// behind an extra `obj != nullptr` test retail makes first (+0x48, dirty 2,
// sub-bit 8). Spelled on the named fields because of that extra test.
// `MeshInstance::_color` (+0x10c) is four floats (Mesh.h keeps the array: a
// `Math::Vector4` member would add its zeroing ctor to MeshInstance's), handed
// to `SetTransformScale` as the `const Vector4&` it is.
Math::Vector4 ColorOf(const Graphics::MeshInstance& mi) {
    return Math::Vector4(mi._color[0], mi._color[1], mi._color[2], mi._color[3]);
}

void BindMaterialImage(Video::Context& context, const Video::ImageBuffer* obj) {
    if (obj != nullptr && obj != context._imageBuffers[3]) {
        context._dirty |= RtVideo::kDirtyImageBuffers;
        context._imageBufferDirty |= 8u;
        context._imageBuffers[3] = obj;
    }
}

// The index binding: `Context::SetIndexBuffer` (0x180019fb0, no C++ body) —
// compare, dirty 0x10, the offset stored BEFORE the pointer as retail does.
void BindIndexBuffer(Video::Context& context, const Video::IndexBuffer* buffer, int32_t first) {
    if (context._indexBuffer == buffer && context._indexOffset == first)
        return;
    context._dirty |= RtVideo::kDirtyIndexBuffer;
    context._indexOffset = first;
    context._indexBuffer = buffer;
}

// The draw itself: `obj = ctx->_contextData; obj->vtable[12](obj, 0, rec->_f00,
// rec->_f04, rec->_f08, rec->_f0c, last)` — `Context::DrawIndexed`
// (0x1800f22.. , context.cpp: `ContextData::DrawIndexed`) inlined. SEVEN values —
// two of them on the stack — and Ghidra's C output prints four.
void IssueDraw(Video::Context& context, const Graphics::GraphicsMesh::TriangleList& rec,
               int32_t last) {
    context.DrawIndexed(0, rec._f00, rec._f04, rec._f08, rec._f0c, last);
}

}  // namespace

// ============================================================================
// ?Draw@MeshInstance@Graphics@@UEBAX…                retail 0x180100960
// ============================================================================
// Arguments, from the mangled name and confirmed against the prologue:
//   RCX this, RDX Context&, R8 Camera&, R9 ExternalPtr<MaterialInstance>,
//   [+0x28] float t, [+0x30] int flags, [+0x38] int mode, [+0x40] int pass.
//
// *** THE CAMERA AND THE MATERIAL ARE NEVER READ. *** R8 and R9 are overwritten
// in the first fifteen instructions and nothing reloads them from the home
// slots. Transcribed as unused rather than "obviously used somewhere".
//
// THE POSE BLOCK IS BEHIND `pass == 1` (`CMP R13D,1 / JNZ`). For any other pass
// the body skips straight to the colour/tuning state and the draw, so the bone
// matrices and the world transform are submitted once per object and not once
// per pass.
//
// The buffer triple comes off `this->_mesh->_detailLevels` (`[this+0xc0]` then
// `+0x50`), re-read from the object EVERY time — four separate reloads in
// retail, kept literal. LEFT: `_detailLevels` is a raw header (GraphicsMesh.h),
// so each reload views it once as its element type.
// ============================================================================

namespace Graphics {

namespace {
const GraphicsMesh::DetailLevel* DetailLevels(const GraphicsMesh* mesh) {
    return mesh->_detailLevels._data;
}
}  // namespace

void MeshInstance::Draw(Video::Context& context, const Camera& camera,
                        ExternalPtrHandle material, float t, int32_t flags,
                        int32_t mode, int32_t pass) const {
    (void)camera; (void)material;                  // R8/R9, never reloaded

    const int32_t node = (flags >> 12) & 0xf;      // SAR 0xc / AND 0xf
    const int32_t sub  = flags & 0xfff;            // record index, stride 0x18

    const GraphicsMesh::DetailLevel& level = DetailLevels(GetMesh().Get())[node];
    const GraphicsMesh::TriangleList& rec = level._triangleLists._data[sub];

    if (pass == 1) {
        const int32_t boneNode = rec._f14;

        Math::Transform world;                     // 0x1c

        if (boneNode < 0) {
            world = LerpTransform(_transform._current, _transform._previous, t);

            // GetBones is handed the FIRST of the two Transforms verbatim (the
            // seven dwords at +0x84), NOT the lerped one — retail copies
            // `[RBX+0x00 .. RBX+0x18]` into the argument buffer at 0x180100b12.
            const Math::Transform bindPose = _transform._current;

            // SKINMEMO (NOT RETAIL, skin_memo.h): inside a Renderer::Draw the
            // same bones from the same inputs, computed once per key. SetBones
            // copies exactly `count` matrices, so retail's zeroing of the rest
            // of its 48-matrix stack buffer never reaches the Context.
            int32_t memoCount = 0;
            const Math::Matrix* const memo =
                SkinMemo::kEnabled ? SkinMemo::Bones(*this, t, bindPose, kBoneMatrices, &memoCount)
                                   : nullptr;
            if (memo != nullptr) {
                context.SetBones(memoCount, memo);
            } else {
                // --- the SKINNED arm: pose 48 bones from this object's own state
                Math::Matrix bones[kBoneMatrices];     // 48 x 0x40, zeroed
                const int32_t count = GetBones(t, bindPose, bones);
                context.SetBones(count, bones);
            }
        } else {
            // --- the RIGID arm: one node transform out of the pose array
            const InterpolatedState<Math::Transform>* base;
            if (_animation._instanced) base = InstancedPoses(*this);   // instanced
            else                       base = _nodes._data;
            const InterpolatedState<Math::Transform>& pair = base[boneNode];
            world = LerpTransform(pair._current, pair._previous, t);
        }

        const Math::Matrix matrix = world.AsMatrix();
        context.SetTransformScale(0, matrix, ColorOf(*this));
    }

    // ---- the fade colour ----------------------------------------------------
    // `alpha = t * _opacity + (1 - t) * _opacityPrev`, in that order:
    // XMM9 = t * [0x7c] is computed FIRST and the (1-t) term is added to it.
    const float alpha = (t * _opacity) + ((1.0f - t) * _opacityPrev);
    context.SetColor(Math::Vector4(1.0f, 1.0f, 1.0f, alpha));

    // ---- the material object, bound only when `mode != 1` -------------------
    if (mode != 1) {
        BindMaterialImage(context, level._image.Get());
    }

    context.SetInverseScale(AsFloat(kTune0), AsFloat(kTune1), AsFloat(kTune2),
                            AsFloat(kNegOne));

    // ---- the buffer bindings ------------------------------------------------
    // `this->_mesh->_detailLevels` is RE-READ before each of the two, which is
    // why it is loaded twice below rather than reused from `level`.
    {
        const GraphicsMesh::DetailLevel& l1 = DetailLevels(GetMesh().Get())[node];
        const bool skinned = rec._f14 < 0;
        context.SetVertexBuffer(skinned ? &l1._skinVertexBuffer : &l1._vertexBuffer,
                                0, 0, skinned ? 0x20 : 0x18);

        const GraphicsMesh::DetailLevel& l2 = DetailLevels(GetMesh().Get())[node];
        BindIndexBuffer(context, skinned ? &l2._skinIndexBuffer : &l2._indexBuffer, 0);
    }

    IssueDraw(context, rec, pass);
}

}  // namespace Graphics

// ============================================================================
// ?Draw@EdgeMeshInstance@Graphics@@UEBAX…            retail 0x1800a3150
// ============================================================================
// The edge-mesh twin. Four differences from the body above, every one of them
// something a "same but for edges" assumption would get wrong:
//
//   1. The flags word is split at BIT 15, not bit 12, and the high bit selects
//      between TWO record arrays — `_mesh->_edgeLists` (+0x158) when set and
//      `_mesh->_triangleLists` (+0x148) when clear — rather than indexing one.
//   2. The pose source is a SEPARATE MeshInstance, `_followed` (+0xc8), not this
//      object. When it is non-null and the record's node index is >= 0, the
//      transform comes from `MeshInstance::GetNodeToWorld(node, t)` rather than
//      from a lerp; when it is NULL the object falls back to its own
//      `_transform` / `_prevTransform` AND uses a UNIT scale vector instead of
//      the mesh's — `(1, 1, 1, 0)`, four immediates at 0x1800a350a.
//   3. The tuning float4's w is `1.0f / (float)Video::Device::_singleton->
//      backBufferWidth` — CVTDQ2PS + DIVSS — not MeshInstance's -1.0f constant.
//      That is the edge-thickness-in-pixels term.
//   4. The colour float4 is a flat `(1, 1, 1, 1)`: there is no fade alpha, so
//      neither `_opacity` nor `_opacityPrev` is read, and the last argument of
//      the draw call is the LITERAL 1 rather than the pass number.
//
// The bone buffer's 48 matrices and the 0x18 record stride are the same.
// B.4 (X-RL): `_mesh` is the typed `Object::ExternalPtr<EdgeMesh>` (Mesh.h).
// ============================================================================

namespace Graphics {

void EdgeMeshInstance::Draw(Video::Context& context, const Camera& camera,
                            ExternalPtrHandle material, float t, int32_t flags,
                            int32_t mode, int32_t pass) const {
    (void)camera; (void)material; (void)mode; (void)pass;

    const bool edgePass = ((static_cast<uint32_t>(flags) >> 15) & 1u) != 0;   // SHR/AND
    const int32_t sub = flags & 0x7fff;

    const EdgeMesh* const mesh = _mesh.Get();
    const GraphicsMesh::TriangleList& rec =
        (edgePass ? mesh->_edgeLists : mesh->_triangleLists)._data[sub];
    const int32_t boneNode = rec._f14;

    const MeshInstance* const poseSource = _followed;

    Math::Transform world;                          // 0x1c
    Math::Vector4   scale;                          // the Vector4 handed to the ctx

    if (boneNode < 0) {
        world = LerpTransform(_transform._current, _transform._previous, t);

        const Math::Transform bindPose = _transform._current;
        // SKINMEMO (NOT RETAIL): as MeshInstance::Draw above, keyed on the
        // followed instance's inputs and this edge mesh's root.
        int32_t memoCount = 0;
        const Math::Matrix* const memo =
            SkinMemo::kEnabled ? SkinMemo::Bones(*poseSource, t, bindPose, kBoneMatrices, &memoCount)
                               : nullptr;
        if (memo != nullptr) {
            context.SetBones(memoCount, memo);
        } else {
            // --- SKINNED: pose 48 bones through the OTHER MeshInstance -------
            Math::Matrix bones[kBoneMatrices];          // zeroed
            const int32_t count = poseSource->GetBones(t, bindPose, bones);
            context.SetBones(count, bones);
        }

        scale = ColorOf(*_followed);                 // re-read from +0xc8, +0x10c
    } else if (poseSource != nullptr) {
        // --- RIGID, driven by the mesh instance ------------------------------
        world = _followed->MeshInstance::GetNodeToWorld(boneNode, t);
        scale = ColorOf(*poseSource);                // +0x10c
    } else {
        // --- RIGID, standalone: lerp our own state and use a UNIT scale ------
        world = LerpTransform(_transform._current, _transform._previous, t);
        scale.x = 1.0f; scale.y = 1.0f; scale.z = 1.0f; scale.w = 0.0f;
    }

    const Math::Matrix matrix = world.AsMatrix();
    context.SetTransformScale(0, matrix, scale);

    context.SetColor(Math::Vector4(1.0f, 1.0f, 1.0f, 1.0f));

    // `CVTDQ2PS` on the device width, then `1.0f / that`. A signed int-to-float
    // conversion, then a single DIVSS — not a multiply by a precomputed
    // reciprocal, which would round differently.
    const float deviceWidth = static_cast<float>(::Video::Device::_singleton->backBufferWidth);
    context.SetInverseScale(AsFloat(kTune0), AsFloat(kTune1), AsFloat(kTune2),
                            1.0f / deviceWidth);

    // ---- the buffer bindings ------------------------------------------------
    // Four (buffer, stride) pairs, chosen by `edgePass` and the record's sign.
    // The base is `this->_mesh`, RE-READ from the object for each of the two
    // bindings exactly as retail reloads it.
    {
        const EdgeMesh* const g1 = _mesh.Get();
        const bool skinned = boneNode < 0;
        const Video::VertexBuffer* const vb =
            edgePass ? (skinned ? &g1->_edgeSkinVertexBuffer : &g1->_edgeVertexBuffer)
                     : (skinned ? &g1->_skinVertexBuffer     : &g1->_vertexBuffer);
        const int32_t vbStride = edgePass ? (skinned ? 0x20 : 0x10)
                                          : (skinned ? 0x10 : 0x08);
        context.SetVertexBuffer(vb, 0, 0, vbStride);

        const EdgeMesh* const g2 = _mesh.Get();
        const Video::IndexBuffer* const ib =
            edgePass ? (skinned ? &g2->_edgeSkinIndexBuffer : &g2->_edgeIndexBuffer)
                     : (skinned ? &g2->_skinIndexBuffer     : &g2->_indexBuffer);
        BindIndexBuffer(context, ib, 0);
    }

    IssueDraw(context, rec, 1);                     // the LITERAL 1, not `pass`
}

}  // namespace Graphics
