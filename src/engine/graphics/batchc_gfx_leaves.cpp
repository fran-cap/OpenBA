// ============================================================================
// src/engine/graphics/batchc_gfx_leaves.cpp
//   BATCH C — the named Graphics leaves today's vtable batches left on the DLL
// ============================================================================
//
// Ten bodies, every one of them a leaf of the cutover closure and every one of
// them previously reached BY NAME from a ported caller (the "NOT PORTED,
// reached by name" comments in lights_vtable.cpp / graphicsmesh_vtable.cpp).
// Those comments are corrected AT THEIR SOURCE by this batch (PLAN.md §6
// step 8); see the end of this banner for the list.
//
//   retail       ord   export name
//   -----------  ----  ---------------------------------------------------
//   0x180105240  2279  ?DrawItemList@Renderer@Graphics@@QEBAXAEAVContext@Video@@HHH@Z
//   0x1800a48e0  3386  ?PlatformSerialize@GraphicsMesh@Graphics@@QEAAXAEAVSerializer@IO@@@Z
//   0x180106050  2135  ?CreateTargets@SpotLight@Graphics@@AEAAXXZ
//   0x180103c70   643  ??1Particle@Graphics@@UEAA@XZ
//   0x1800a51c0   645  ??1ParticleSystem@Graphics@@UEAA@XZ
//   0x1800160a0    38  ??0Animation@Graphics@@QEAA@AEBV01@@Z   (the COPY ctor)
//   0x1800fb290   525  ??1Animation@Graphics@@QEAA@XZ
//   0x1800a2780   580  ??1EdgeMesh@Graphics@@UEAA@XZ
//   0x180102eb0  2402  ?GenerateTextures@MaterialInstance@Graphics@@QEBAXAEAVContext@Video@@H@Z
//   0x1800a5520   646  ??1ParticleSystemInstance@Graphics@@UEAA@XZ
//
// Every one read from the shipped retail Runtime-x64.dll's DISASSEMBLY
// (ghidra-mcp disassemble_function, program="Runtime-x64.dll"), never from
// Ghidra's decompiled C. Every export name was read back out of the export
// directory by VA with pefile before this file was written.
//
// ---------------------------------------------------------------------------
// ⚠ ATTENDANCE IS ZERO ON A DLL-PRESENT RUN, AND THAT IS STRUCTURAL
// ---------------------------------------------------------------------------
// MEASURED, 2026-09-12, one `BANISHED_AUTOSTART=1 the diff oracle armed
// BANISHED_PAUSEMENU=600 BANISHED_PAUSEMENU_QUIT=120` run to the in-game world
// and a clean self-quit (exit 0, empty standalone_crash.log, 154 `startTown`
// lines): NONE of the ten sites below entered, so none appears in the diff oracle
// summary at all.
//
// THAT IS NOT "the ports are dead code". It is what the seam does while the DLL
// is loaded. Every caller of `DrawItemList` in this tree is one of the five
// light `Draw` arms in lights_vtable.cpp, and those are VTABLE SLOTS of
// ??_7{Ambient,Spot,Sun}Light@Graphics@@6B@ — rows the mint has not promoted.
// With the DLL present those tables still hold the DLL's own bodies, so the
// DLL's `SunLight::Draw` reaches the DLL's `DrawItemList` by rel32 and never
// consults the port registry. The same holds for `Context::SetLight` (reached
// only from those same arms) and for the four destructors, which the DLL's own
// deleting-dtor slots call internally. The proof that the seam itself is fine
// is in the same log: `Graphics::SunLight::SunLight` and
// `Graphics::ParticleSystemInstance::ParticleSystemInstance` — the NAME-reached
// members of the very same classes — entered 123 times between them.
//
// So these ten are verified by TRANSCRIPTION against the disassembly, and their
// first execution will be at cutover, where the mint's vtable rows are ours.
// A reader who meets a green the diff oracle summary must not read it as coverage of
// this file (reference_uncalled_port_is_not_verified.md).
//
// ---------------------------------------------------------------------------
// FIVE THINGS THAT WOULD BE WRONG IF READ FROM THE DECOMPILER
// ---------------------------------------------------------------------------
// 1. `Renderer::DrawItemList` TAKES FIVE VALUES (this + four ints) and the
//    fifth lives in its own home slot at [RBP+0xc0]. It is the SHADER INDEX:
//    the body indexes `item->material->_programs[arg5]` with it and hands it to
//    `MaterialInstance::Setup` and, as the 7th argument, to the geometry's
//    `Draw`. lights_vtable.cpp's banner already recorded the five-value fact
//    for the three Draw arms that tail-jump here; this body is what those five
//    values are FOR.
// 2. The two 4x4 matrices DrawItemList hands `Context::SetCamera` are BUILT ON
//    THE STACK, not copied: the first is the cull camera's `Transform3`
//    (camera+0x64, twelve floats) EXPANDED to a Matrix with an explicit
//    (0,0,0,1) fourth column written as three zeroed floats and one 1.0f read
//    from 0x18016169c; the second is the camera's projection, sixteen dwords
//    copied verbatim. Getting the expansion wrong silently transposes the view.
// 3. `??1Animation@` and `??1Particle@` release `System::String`s with retail's
//    OWN asymmetry: on the LAST reference the slot is left holding `str-8`
//    (a dangling pointer into the freed block) and only on a SURVIVING
//    reference is it nulled. `~Particle` additionally branches on the ZF of the
//    `ADD qword [slot],-8` itself, so a string whose data pointer was exactly 8
//    is not freed. Both are reproduced; neither is a transcription slip.
// 4. `~EdgeMesh` destroys its eight Video buffer headers in DESCENDING address
//    order and alternates IndexBuffer/VertexBuffer PlatformDestruct — the
//    pairs are (verts, indices) in memory but (indices, verts) in teardown.
// 5. `GraphicsMesh::PlatformSerialize` has TWO device-interface arms over the
//    same stream, the shape src/engine/video/shaderprogram.cpp spells out:
//    each arm builds its OWN ReaderWriter and, when its interface is not the
//    active one, calls `Skip()` so the stream still advances past that
//    backend's blob. The arms are NOT interchangeable: arm 0 hands the whole
//    triangle array to the shared helper 0x180014850, arm 1/2 emits the same
//    six dwords per element inline. Collapsing them into one loop would emit
//    the right bytes for the wrong backend.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "graphics/Camera.h"
#include "graphics/Animation.h"          // B.4: Graphics::Animation (the copy ctor / dtor doors)
#include "graphics/GeneratedImageBuffer.h"  // ENDGAME B.3.5 block GC
#include "graphics/GraphicsMesh.h"   // ENDGAME B.3.5 block G
#include "graphics/Light.h"          // Graphics::SpotLight -- block GC
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/xform_memo.h"     // XFORMMEMO: the instance-transform memo (DrawItemList)
#include "io/Serializer.h"
#include "io/Stream.h"                  // B.4: IO::Stream::_encoding / _direction
#include "video/CompanionData.h"        // B.4: Video::{Index,Vertex}BufferData::Serialize
#include "video/Context.h"           // ENDGAME B.3.5 block GC
#include "video/VideoProgram.h"      // B.4: Video::VideoProgram (DrawItemList)
#include "video/ShaderProgram.h"     // B.4: Video::{Vertex,Pixel}Program::_byteCode
// ENDGAME B.3.5 caller batch: the cross-class edges this file opens.
#include "graphics/ParticleInstance.h"  // Graphics::ParticleInstance
#include "video/Buffers.h"              // Video::{Index,Vertex}Buffer
#include "video/Device.h"               // Video::RenderBuffer
#include "collection/Collection.h"      // rtcoll:: the array instantiations
#include "graphics/Particle.h"          // RtGraphics_Emitter_OperatorDelete
#include "../picking/scene_grid.h"                  // rtpickgrid::TransformArrayAssign / ArrayDtorTransform

namespace rtcoll { void MemFree(void* p); }   // src/engine/collection/array.cpp

namespace {

// ---------------------------------------------------------------------------
// Retail vtable constants. Resolved through the NAME (all five are exported
// data symbols — checked by VA with pefile), so a minted/promoted row is used
// where one exists and the DLL's own table otherwise.
// ---------------------------------------------------------------------------
// (ENDGAME B.3.9 GFX: `VtParticleInstance` / `VtParticleSystemInstance` are
// gone with the ??1ParticleSystemInstance door that restamped them.)

// ---------------------------------------------------------------------------
// Named dependencies, called by name (B3R): the class members, and the
// unexported template instantiations under their body names (rtcoll::,
// rtpickgrid::).
// ---------------------------------------------------------------------------

// (ENDGAME B.3.9 GFX: `GeometryDtor`, the ??1Geometry door call of the retired
// ??1ParticleSystemInstance door, is gone; the real dtor chains ~Geometry.)

// (ENDGAME B.3.9 GFX: `ExternalDtor`, the ??1External door the retired ??1EdgeMesh
// door tail-called, is gone; the real ~EdgeMesh chains ~External.)

// Video::{Index,Vertex}Buffer::PlatformDestruct — 0x1800f3760 / 0x1800f58c0:
// member calls on EdgeMesh's typed buffers (B.4; the two wrappers are gone).

// `??2ImageBuffer@Video@@SAPEAX_K@Z` / `??2RenderBuffer@Video@@SAPEAX_K@Z` —
// the class operator news. CreateTargets INLINES both (Enter(alloc+0x640);
// Allocate(alloc, n, 8); Leave), which is instruction for instruction what
// these two exported statics are; B.4: CreateTargets calls them by name, and
// the RenderBuffer members (`PlatformConstruct`, `Create`) likewise.

// Video::Context setters DrawItemList drives, and
// `?Setup@MaterialInstance@Graphics@@QEBAXAEAVContext@Video@@H@Z` 0x180102c90:
// DEFINED members, called by name (B.4: the `void*` wrappers are gone). THE
// DOOR DECIDED THE OVERLOAD — `?SetTransform@…QEAAXHAEBVMatrix@Math@@@Z`
// 0x18001a6b0 is the two-argument setter, NOT `SetTransformScale` (0x18001a400).

// ---------------------------------------------------------------------------
// The System::String release idiom, spelled once. Retail open-codes it at the
// three name slots of ~Animation.
//
//   if (!s) return;
//   if (--s[-1] != 0) { *slot = nullptr; return; }     // a reference survives
//   *slot = s - 8;                                      // LAST reference:
//   MemFree(*slot);                                     // slot keeps str-8
//
// THE SLOT IS DELIBERATELY LEFT DANGLING ON THE FREE ARM. That is what
// 0x1800fb2c2/0x1800e040e/0x180103e17 do, and vtbatch7b_stream_run_win.cpp's
// ReleaseString already records the same shape. (~Particle's extra ZF twist
// lives with ~Particle in animation.cpp.) B.4: the slot is the `char*` field
// itself and the count is `RtSystem::StringRefCount` (String.h).
// ---------------------------------------------------------------------------
void ReleaseString(char** slot) {
    char* const s = *slot;
    if (s == nullptr) return;
    int32_t& rc = RtSystem::StringRefCount(s);
    --rc;
    if (rc != 0) { *slot = nullptr; return; }
    char* const blk = s - 8;
    *slot = blk;                                 // ADD qword [slot],-8
    rtcoll::MemFree(blk);
}

}  // namespace

// ############################################################################
// Graphics::MaterialInstance::GenerateTextures
// ?GenerateTextures@MaterialInstance@Graphics@@QEBAXAEAVContext@Video@@H@Z
// retail 0x180102eb0, ordinal 2402
// ############################################################################
//
// The render-TO-texture sibling of `RenderTextures` (0x180102df0, ported in
// wave9_lane_gfx.cpp): same walk over `_textures` (0x10 stride, count at
// +0x58), but gated on `hasRenderTexture` (+0x24, not +0x25), selecting
// entries whose kind is 4 (not 5) and dispatching vtable slot +0x50 (not
// +0x58) with (Context&, int).
//
// THE SAME TWO FAITHFUL UGLINESSES AS ITS SIBLING, both kept:
//   * `obj->+0x2c` is dereferenced BEFORE `obj` is null-checked (0x180102eea
//     vs 0x180102f02). A null entry faults in retail and faults here.
//   * the loop bound `*(int*)(this+0x58)` is RE-READ every iteration
//     (0x180102f1c), not hoisted, so a slot-0x50 handler that appends a
//     texture is seen.
//
// ORACLE: none. The dispatch renders into a GPU target; running the shipped
// body afterwards would render a second time. the diff oracle is the effectiveness
// control — almost every call returns at the +0x24 test.
// ############################################################################
// ENDGAME B.3.5 block GC: THE BODY MOVED to
// `Graphics::MaterialInstance::GenerateTextures`, in this file's existing
// `namespace Graphics` block below. The door keeps its name and both its
// registrations (by name and at 0x180102eb0).
//
// THE DEAD EFFECTIVENESS CONTROL IS GONE, for the same reason its sibling's was
// in wave9_lane_gfx.cpp: `const bool didWork = …` had no reader anywhere in the
// tree. The banner above still calls it "the diff oracle", and that sentence was
// describing a variable nothing printed.
//
// B.4: the door's one caller (DrawItemList below) calls the member by name;
// the `void*` door has no other reader and is gone.

// ############################################################################
// Graphics::ParticleSystemInstance::~ParticleSystemInstance
// ??1ParticleSystemInstance@Graphics@@UEAA@XZ   retail 0x1800a5520, ordinal 646
// ############################################################################
//
//   this->vtable = ??_7ParticleSystemInstance@
//   if (this->_resource->[0x20] == 0 && this->_emitter)    // UNGUARDED deref
//       { ArrayDtor(_emitter); operator delete(_emitter); }
//   this->vtable = ??_7ParticleInstance@
//   ParticleInstance::RemoveState(this)
//   tail ~Geometry(this)
//
// `this->_resource` (+0xc0) IS DEREFERENCED WITHOUT A NULL TEST (0x1800a5545).
// The ctor initialises it to null, so a ParticleSystemInstance destroyed before
// anything attached a resource faults in retail. Reproduced, not guarded
// (CLAUDE.md: a guard skips the behaviour rather than restoring it).
// ############################################################################
//
// ENDGAME B.3.9 block GFX: the door is RETIRED with the RT_DELDTOR row
// 0x180020ce0 it served; its body is the real protected
// `~ParticleSystemInstance()` below. The two restamps, RemoveState
// (~ParticleInstance) and the ~Geometry tail are the compiler's chain.
Graphics::ParticleSystemInstance::~ParticleSystemInstance() {
    if (_resource->_active == 0) {                      // +0x20, UNGUARDED, as retail
        RtGraphics::ParticleEmitter* const owned = _emitter;
        if (owned != nullptr) {
            // 0x180021490: ~Array<T> for a trivially destructible T — frees
            // [0] and zeroes [0]/[8]. 0x180020600: the matching class
            // operator delete. Both unexported template instantiations,
            // called by their body names.
            rtcoll::RtColl_ArrayDtor_180021490(&owned->_extra);
            RtGraphics_Emitter_OperatorDelete(owned);
        }
    }
}

// ############################################################################
// ??1ParticleSystem@Graphics@@UEAA@XZ 0x1800a51c0 / ??1Particle@Graphics@@UEAA@XZ
// 0x180103c70 -- the two transcribed DOORS are gone (ENDGAME B.3.9 block GFX).
// Their callers were the retired RT_DELDTOR rows 0x180020a70 / 0x180020160 and
// each other. The bodies live on as `~ParticleSystem` (the owned pool walk,
// vtbatch7_animgroup_particlesystem.cpp) and `~Particle` (`ReleaseFields`,
// animation.cpp); retail's inlined ~External is the compiler's ~External.
// ############################################################################
// ############################################################################
// Graphics::Animation::Animation(const Animation&)
// ??0Animation@Graphics@@QEAA@AEBV01@@Z         retail 0x1800160a0, ordinal 38
// ############################################################################
//
// The COPY constructor, not the default one. Returns `this` in RAX.
//
//   this->[0x00] = 0; this->[0x08] = 0;  ArrayAssign_18001b270(this,   &src)
//   AcquireString(&this->[0x10], src->[0x10])     ; +0 then ++refcount
//   AcquireString(&this->[0x18], src->[0x18])
//   AcquireString(&this->[0x20], src->[0x20])
//   +0x28 int, +0x2c byte, +0x30/+0x34/+0x38 int, +0x3c byte,
//   +0x40/+0x44/+0x48/+0x4c/+0x50 int, +0x58 qword   — all plain copies
//   this->[0x60] = 0; this->[0x68] = 0;  ArrayAssign_18001b360(this+0x60, src+0x60)
//   return this
//
// THE TWO ARRAY MEMBERS ARE ZEROED BEFORE THE ASSIGN, not after: the assign
// helper reads the destination's `_data`/`_size` to decide whether to reuse the
// buffer, so a copy ctor that did not clear them first would free the SOURCE
// object's memory. Both helpers are unexported template instantiations still on
// the DLL (0x18001b270 for the +0x00 element type, 0x18001b360 for the +0x60
// one — the latter is the `TransformArrayAssign` picking/scene_grid.cpp names).
//
// +0x54 IS NOT COPIED. Retail copies +0x50 as a dword and then +0x58 as a
// qword, leaving the four bytes at +0x54 holding whatever the allocator handed
// back. Left alone here for the reason Camera.h's `_pad24` note gives: zeroing
// it would make our copy differ from retail's on a byte retail never writes.
// ############################################################################
//
// B.4: on `Graphics::Animation`'s named fields. The door keeps its `void*`
// signature (array.cpp's element ops and the unit test's link stub hold it),
// so each side is viewed once as the class (LEFT); ??0 stays a door, not a
// real ctor (W1 brief: Animation's ctors change only with object-identical
// proof). The float fields are plain 4-byte moves either way.
extern "C" void* RtGraphics_Animation_CopyCtor(void* selfv, const void* srcv) {
    Graphics::Animation* const self = static_cast<Graphics::Animation*>(selfv);  // CAST-OK: elem_stride
    const Graphics::Animation* const src = static_cast<const Graphics::Animation*>(srcv);  // CAST-OK: elem_stride

    self->_events._data     = nullptr;           // +0x00 \ both qwords zero
    self->_events._size     = 0;                 // +0x08 |
    self->_events._capacity = 0;                 //       /
    rtcoll::RtColl_ArrayAssign_18001b270(&self->_events, &src->_events);   // 0x18001b270

    // The three refcounted names, ASCENDING — the mirror of the destructor's
    // descending release. Each slot is zeroed first and only then overwritten,
    // exactly as retail does it (`MOV [RBX+off],RSI` with RSI == 0, then the
    // conditional store and `INC dword [RAX-4]`).
    System::String Graphics::Animation::* const kNames[3] = {
        &Graphics::Animation::_name,             // +0x10
        &Graphics::Animation::_meshName,         // +0x18
        &Graphics::Animation::_sourceName,       // +0x20
    };
    for (int k = 0; k < 3; ++k) {
        (self->*kNames[k])._data = nullptr;
        char* const s = (src->*kNames[k])._data;
        if (s != nullptr) {
            (self->*kNames[k])._data = s;
            ++RtSystem::StringRefCount(s);
        }
    }

    self->_instanceCount = src->_instanceCount;  // +0x28
    self->_instanced     = src->_instanced;      // +0x2c
    self->_startFrame    = src->_startFrame;     // +0x30
    self->_endFrame      = src->_endFrame;       // +0x34
    self->_rate          = src->_rate;           // +0x38
    self->_looping       = src->_looping;        // +0x3c
    self->_loopFrame     = src->_loopFrame;      // +0x40
    self->_frameRate     = src->_frameRate;      // +0x44
    self->_loopScale     = src->_loopScale;      // +0x48
    self->_boneCount     = src->_boneCount;      // +0x4c
    self->_frameCount    = src->_frameCount;     // +0x50
    // +0x54 deliberately untouched — see the banner.
    self->_duration      = src->_duration;       // +0x58

    self->_keys._data     = nullptr;             // +0x60 \ both qwords zero
    self->_keys._size     = 0;                   // +0x68 |
    self->_keys._capacity = 0;                   //       /
    rtpickgrid::TransformArrayAssign(&self->_keys, &src->_keys);   // 0x18001b360

    return self;                                          // MOV RAX,RBX
}

// ############################################################################
// Graphics::Animation::~Animation
// ??1Animation@Graphics@@QEAA@XZ                retail 0x1800fb290, ordinal 525
// ############################################################################
//
//   ArrayDtor_18001c100(this + 0x60)        ; the Transform array
//   ReleaseString(&this->[0x20])            ; the three names, in DESCENDING
//   ReleaseString(&this->[0x18])            ; offset order — the mirror of the
//   ReleaseString(&this->[0x10])            ; copy ctor's ascending acquire
//   tail ArrayDtor_18001c020(this)          ; the +0x00 array
//
// NO VTABLE: `??1Animation@...QEAA@XZ` is a Q, not a U, and the body stamps
// nothing. Graphics::Animation is plain data, like AnimationManager.
// ############################################################################
//
// B.4: on the named fields; the door keeps its `void*` signature (LEFT, as
// the copy ctor's).
extern "C" void RtGraphics_Animation_Dtor(void* selfv) {
    Graphics::Animation* const self = static_cast<Graphics::Animation*>(selfv);  // CAST-OK: elem_stride

    rtpickgrid::ArrayDtorTransform(&self->_keys);         // 0x18001c100, +0x60

    ReleaseString(&self->_sourceName._data);              // +0x20
    ReleaseString(&self->_meshName._data);                // +0x18
    ReleaseString(&self->_name._data);                    // +0x10

    rtcoll::RtColl_ArrayDtor_18001c020(&self->_events);   // JMP 0x18001c020, +0x00

}

// ############################################################################
// Graphics::EdgeMesh::~EdgeMesh
// ??1EdgeMesh@Graphics@@UEAA@XZ                 retail 0x1800a2780, ordinal 580
// ############################################################################
//
//   this->vtable = ??_7EdgeMesh@
//   ArrayDtor_180013e70(&_edgeLists)      +0x158
//   ArrayDtor_180013e70(&_triangleLists)  +0x148
//   IndexBuffer::PlatformDestruct (+0x128)   VertexBuffer::PlatformDestruct (+0x108)
//   IndexBuffer::PlatformDestruct (+0x0e8)   VertexBuffer::PlatformDestruct (+0x0c8)
//   IndexBuffer::PlatformDestruct (+0x0a8)   VertexBuffer::PlatformDestruct (+0x088)
//   IndexBuffer::PlatformDestruct (+0x068)   VertexBuffer::PlatformDestruct (+0x048)
//   tail ~External(this)
//
// DESCENDING ADDRESS ORDER, INDICES BEFORE VERTS within each pair — the exact
// reverse of the constructor's build order (Mesh.h's EdgeMesh banner).
// ############################################################################
//
// ENDGAME B.3.9 block GFX: the door is RETIRED with the RT_DELDTOR row
// 0x1800134b0 it served; its body is the real protected `~EdgeMesh()`. The
// restamp and the tail ~External are the compiler's chain.
//
// B.4: the members by name. The two explicit ~Array calls zero their headers,
// so the typed members' implicit `~Array<TriangleList>` that follows is the
// same no-op the raw headers' was.
Graphics::EdgeMesh::~EdgeMesh() {
    // 0x180013e70 — Array<T>::~Array for the two list arrays. An unexported
    // template instantiation, called by its body name.
    rtcoll::RtColl_ArrayDtor_180013e70(&_edgeLists);
    rtcoll::RtColl_ArrayDtor_180013e70(&_triangleLists);

    // B.4 wave Y: the eight buffers are destroyed by their own ~IndexBuffer /
    // ~VertexBuffer (0x1800f3480 / 0x1800f55e0, `JMP PlatformDestruct`) after
    // this body, in reverse declaration order: +0x128, +0x108, ... +0x048,
    // retail's order above.
}

// ############################################################################
// Graphics::SpotLight::CreateTargets
// ?CreateTargets@SpotLight@Graphics@@AEAAXXZ    retail 0x180106050, ordinal 2135
// ############################################################################
//
// Builds the shadow-map trio lights_vtable.cpp's SpotLight banner describes:
// +0x148 the colour image, +0x150 the depth image, +0x158 the render target.
//
//   p = ImageBuffer::operator new(0x68); this->_shadowMap = p ? (ImageBuffer(p), p) : 0
//   this->_shadowMap->vt[9](this->_shadowMap, 0, 0, 5, 0x40, 0x400, 0x400, 1,1,1,1, null)
//   p = ImageBuffer::operator new(0x68); this->_shadowDepth = p ? (ImageBuffer(p), p) : 0
//   this->_shadowDepth->vt[9](..., 0, 1, 7, 0x40, 0x400, 0x400, 1,1,1,1, null)
//   p = RenderBuffer::operator new(0x30)
//   if (p) { p[0]=p[1]=p[2]=0; RenderBuffer::PlatformConstruct(p); }
//   this->_shadowTarget = p
//   tail RenderBuffer::Create(p, this->_shadowMap, this->_shadowDepth)
//
// The eleven Create arguments are in RETAIL'S OWN ORDER, read from the stack
// stores at 0x1801060d5..0x180106112 — NOT assumed from the field order, which
// they do not follow (the trap imagebuffer_derived.cpp documents for
// ReflectionImageBuffer::Serialize). `usage` 1 + `format` 7 on the second call
// is what makes it the DEPTH buffer; 0x400 x 0x400 is the 1024² shadow map.
//
// THE CTOR'S RETURN VALUE IS NOT USED. Retail stores the ctor's RAX
// (`MOV RCX,RAX ; MOV [RSI+0x148],RCX`), and the ctor returns `this`, so the
// stored pointer is the allocation either way — spelled with the allocation
// here for the reason wave23_lane_gfxbody.cpp gives.
//
// AN UNGUARDED DEREF, KEPT: when `operator new` returns null the field is set
// to null and `MOV RAX,[RCX]` at 0x1801060d2 immediately reads through it.
// Retail faults there; so does this.
// ############################################################################

// ############################################################################
// Graphics::GraphicsMesh::PlatformSerialize
// ?PlatformSerialize@GraphicsMesh@Graphics@@QEAAXAEAVSerializer@IO@@@Z
// retail 0x1800a48e0, ordinal 3386
// ############################################################################
//
// TWO device-interface arms over one stream, the shape
// src/engine/video/shaderprogram.cpp spells out. Arm 0 runs when
// `Video::Device::_singleton->activeInterface == 0`; arm 1 runs when it is 1 or
// 2. Each builds its OWN ReaderWriter; the arm whose interface is not active
// calls `Skip()` so the stream still advances past that backend's blob.
//
// The detail level is the 0xb0-stride record Mesh.h's GraphicsMeshView pins
// (data at mesh+0x50, count at mesh+0x58). Within it:
//   +0x20 / +0x40 / +0x60 / +0x80  four Video buffer objects, each serialized
//                                  through ITS OWN vtable slot +0x30, and in
//                                  the order 0x40, 0x20, 0x80, 0x60 — NOT
//                                  ascending, and not a transcription slip
//                                  (the same crossed order EdgeMesh::SetMaterials
//                                  has, Mesh.h's note).
//   +0x98 / +0xa0                  Collection::Array<Triangle>, 0x18 stride
//   +0xa8                          a trailing int
//
// THE VERSION GATE READS THE **OUTER** READER/WRITER, not the arm's own
// (0x1800a4961: `MOV RCX,[R15+8]`). When it says >= 1 each arm emits one extra
// int whose value is DISCARDED on read — a local initialised to zero.
// ############################################################################
namespace {

// The four buffer calls: `MOV RCX,[level+X] ; MOV RAX,[RCX] ; CALL [RAX+0x30]`
// -- slot 6 `Serialize` of the buffer's companion (`_deviceData`, what
// PlatformConstruct wrote). The dereference is unguarded, which is faithful.
// B.4: the buffers are the real `Video` classes; their `platformData` is typed.
void SerializeIndexBuffer(Video::IndexBufferData* deviceData, IO::Serializer& sub) {
    deviceData->Serialize(&sub);
}
void SerializeVertexBuffer(Video::VertexBufferData* deviceData, IO::Serializer& sub) {
    deviceData->Serialize(&sub);
}

}  // namespace

// ############################################################################
// ENDGAME B.3.5 step 5, block G - the body MOVED onto
// `Graphics::GraphicsMesh::PlatformSerialize`, and every offset it walked by
// hand is now a named field of `GraphicsMesh` / `GraphicsMesh::DetailLevel`
// (`src/engine/graphics/GraphicsMesh.h`, authored by this block). The eleven
// `kLvl*` constants this file derived independently AGREE with that header field
// for field - stride 0xb0, the Array<TriangleList> at +0x98/+0xa0, the four
// buffer `_deviceData` qwords at +0x20/+0x40/+0x60/+0x80 and the tail int at
// +0xa8 - which is the third independent reading of the 0xb0 record.
//
// *** RULE B5-11 LIVES ON THIS FUNCTION. *** Its registration was once stripped
// from the PERF build because the `RtGraphicsBatchCLeavesRegister()` call that
// made it sat inside a `printf` argument, whose whole expression
// `src/app/log_strip.h` compiles to `((void)0)`; boot then died on a
// VCALL-of-0 (defect 3, fixed `1a4200c3`). Nothing below introduces such a call,
// and the perf build is a gate item for this commit.
// ############################################################################
namespace Graphics {

// ---------------------------------------------------------------------------
// ?GenerateTextures@MaterialInstance@Graphics@@QEBAXAEAVContext@Video@@H@Z
// retail 0x180102eb0 — ENDGAME B.3.5 block GC. The evidence, the two faithful
// uglinesses and the oracle note are in the door's banner above.
//
// TWO RAW BINDINGS BECAME TYPED ONES (item 6), exactly as in the sibling
// `RenderTextures`: the `textures._data + i*0x10` then `+0x08` walk is
// `_textures._data[i].image`, and the hand-inlined `(*(void***)obj)[0x50/8]` is
// slot TEN of `Graphics::GeneratedImageBuffer` — `Generate(context, generation)`
// — still dispatched VIRTUALLY, so an object wearing the minted table is reached
// the same way it was. 0x50/8 == 10 is checked against
// GeneratedImageBuffer.h's slot list.
//
// NOTE THE ASYMMETRY WITH ITS SIBLING, WHICH IS RETAIL'S: `RenderTextures` tests
// `_textures._size <= 0` before the loop and this one does NOT — it goes
// straight from the `+0x24` test into the loop, whose own bound then ends it.
// ---------------------------------------------------------------------------
void MaterialInstance::GenerateTextures(Video::Context& context, int32_t arg) const {
    if (_hasRenderTexture == 0) {
        return;
    }

    for (int32_t i = 0; i < _textures._size; ++i) {
        const Video::ImageBuffer* const obj = _textures._data[i].image;

        if (obj->_kind != 4)                     // +0x2c, the usage
            continue;
        if (obj == nullptr)                      // retail's order: after the deref
            continue;

        // Slot 10 (0x50/8) — a virtual call on the real class. LEFT: usage 4
        // is what makes the image a `GeneratedImageBuffer`; the texture array
        // holds the base pointer, so the downcast is the one view.
        static_cast<const GeneratedImageBuffer*>(obj)->Generate(&context, arg);
    }
}

void GraphicsMesh::PlatformSerialize(IO::Serializer& serializer) {
    // `if (!stream->encoding->vt[0](encoding)) return;` — slot 0 of the
    // stream's `IO::Encoding`, `IsBinary`: only a binary stream carries
    // platform data at all.
    if (!serializer._stream->_encoding->IsBinary()) {
        return;
    }

    const int32_t levelCount = _detailLevels._size;                  // +0x58

    for (int arm = 0; arm < 2; ++arm) {
        // Each arm's own ReaderWriter: `{ stream, 0 }` then the encoding's
        // CreateReader / CreateWriter (version 0), and `if (rw) delete rw` at
        // the end of the arm -- IO::Serializer's ctor and dtor, inlined.
        IO::Serializer sub(*serializer._stream, 0);
        IO::ReaderWriter* const rw = sub._readerWriter;

        // `Video::Device::_singleton->activeInterface` (+0xF8), unguarded.
        const int32_t iface = ::Video::Device::_singleton->activeInterface;
        const bool active = (arm == 0) ? (iface == 0) : (iface == 1 || iface == 2);
        if (!active) {
            rw->Skip();
            continue;
        }

        // The gate reads the OUTER reader/writer — see the banner.
        if (serializer._readerWriter->GetVersion() >= 1) {
            int32_t discarded = 0;
            rw->SerializeInt(discarded, nullptr);
        }

        for (int32_t i = 0; i < levelCount; ++i) {
            // `this->[0x50]` is RE-READ every iteration (0x1800a49a0 /
            // 0x1800a4b30), so a slot-0x30 handler that reallocates the level
            // array is seen. Not hoisted.
            DetailLevel* const level =
                _detailLevels._data + i;

            int32_t n = level->_triangleLists._size;                  // +0xa0
            rw->SerializeInt(n, nullptr);
            if (sub._stream->_direction == IO::kReading)
                rtcoll::RtColl_ArraySetSize_1800136f0(&level->_triangleLists, n);  // +0x98

            TriangleList* const triData = level->_triangleLists._data;
            if (arm == 0) {
                // 0x180014850 SerializeTriangleListArray(ctx, count, data) —
                // PORTED (closure_batch2b.cpp).
                RtGraphics_EdgeMesh_SerializeTriangleListArray(sub, n, triData);   // 0x180014850
            } else {
                // The same six dwords per element, emitted inline by retail on
                // this arm (0x1800a4b90..0x1800a4c0e, stride 0x18).
                TriangleList* e = triData;
                for (int32_t j = 0; j < n; ++j, ++e) {
                    rw->SerializeInt(e->_baseVertex, nullptr);
                    rw->SerializeInt(e->_vertexCount, nullptr);
                    rw->SerializeInt(e->_startIndex, nullptr);
                    rw->SerializeInt(e->_indexCount, nullptr);
                    rw->SerializeInt(e->_materialIndex, nullptr);
                    rw->SerializeInt(e->_node, nullptr);
                }
            }

            // The four buffers, INDEX BEFORE VERTEX in each pair, retail's
            // order (+0x40, +0x20, +0x80, +0x60 = each header's +0x08).
            DetailLevel* const lvl2 =
                _detailLevels._data + i;
            SerializeIndexBuffer (lvl2->_indexBuffer.platformData,      sub);
            SerializeVertexBuffer(lvl2->_vertexBuffer.platformData,     sub);
            SerializeIndexBuffer (lvl2->_skinIndexBuffer.platformData,  sub);
            SerializeVertexBuffer(lvl2->_skinVertexBuffer.platformData, sub);

            DetailLevel* const lvl3 =
                _detailLevels._data + i;
            rw->SerializeInt(lvl3->_maxInstances, nullptr);            // +0xa8
        }
    }
}

// ---------------------------------------------------------------------------
// ?CreateTargets@SpotLight@Graphics@@AEAAXXZ  0x180106050 -- block GC.
// `AEAA` is PRIVATE and this DLL exports its private members; the member is
// PUBLIC in Light.h for the reason block G recorded on MaterialInstance's two
// private finders (an `extern "C"` door cannot be a friend).
//
// The four SpotLight target pointers are still reached at their raw offsets
// through one bridge local: Light.h names them, but every store here is an
// inlined `operator new` plus an unguarded vtable slot-9 `Create`, and the
// arithmetic is what the banner above documents.
// ---------------------------------------------------------------------------
//
// B.4: on the class's typed fields (`_shadowMap` / `_shadowDepth` /
// `_shadowTarget`, Light.h). The placement-new expression's value IS the
// allocation, which is what retail stores.
void SpotLight::CreateTargets() {
    // ---- +0x148, the shadow colour image ----------------------------------
    {
        void* const p = Video::ImageBuffer::operator new(0x68);   // 0x18001e910
        _shadowMap = (p != nullptr) ? ::new (p) Video::ImageBuffer() : nullptr;
        // Slot 9 `Create`, by name (B3X W0); faithful: unguarded, and the field
        // is re-read (`MOV RCX,[RSI+0x148]`).
        _shadowMap->Create(0, 0, 5,
                           Video::ImageBuffer::kShadowMap, 0x400, 0x400,
                           1, 1, 1, 1, nullptr);
    }

    // ---- +0x150, the shadow depth image ------------------------------------
    {
        void* const p = Video::ImageBuffer::operator new(0x68);
        _shadowDepth = (p != nullptr) ? ::new (p) Video::ImageBuffer() : nullptr;
        _shadowDepth->Create(0, 1, 7,
                             Video::ImageBuffer::kShadowMap, 0x400, 0x400,
                             1, 1, 1, 1, nullptr);
    }

    // ---- +0x158, the render target -----------------------------------------
    // `Video::RenderBuffer::RenderBuffer` INLINED (the three zeroed qwords, then
    // PlatformConstruct). LEFT: the raw allocation is viewed once as the
    // RenderBuffer the inlined ctor builds in it.
    Video::RenderBuffer* const rb =
        static_cast<Video::RenderBuffer*>(Video::RenderBuffer::operator new(0x30));   // 0x18001f2e0  // LEFT: allocation result of Video::RenderBuffer::operator new: castmetric's tier-I shape does not recognise a class operator new (ledger: decide at the close)
    if (rb != nullptr) {
        rb->colorBuffer = nullptr;   // +0x00
        rb->depthBuffer = nullptr;   // +0x08
        rb->width  = 0;              // +0x10 \ one qword store in retail
        rb->height = 0;              // +0x14 /
        rb->PlatformConstruct();
    }
    _shadowTarget = rb;

    // JMP 0x1800f3f10 -- unguarded, as retail's tail jump is.
    rb->Create(_shadowMap, _shadowDepth);
}

// ---------------------------------------------------------------------------
// ?DrawItemList@Renderer@Graphics@@QEBAXAEAVContext@Video@@HHH@Z 0x180105240
// ENDGAME B.3.5 block GC. The five-value argument list, the two stack-built 4x4
// matrices and the instancing batch are documented in the banner below.
//
// B.4: on the real classes' named fields -- the draw group (`_cullCamera`,
// `_items[list]`), each `RendererDrawItem` (Camera.h), the item's Material
// (`_passes`), the selected `Video::VideoProgram` (`_vertexLayout` /
// `_vertexProgram` / `_pixelProgram`) and each program's `_byteCode._size`
// (+0x48) -- and the Context setters by name. The POD bridge is gone.
// ---------------------------------------------------------------------------
// EXT SEAM (NOT RETAIL, docs/extensions/handins/DRAWBATCH.md): null unless ext.draw_batch is on.
Renderer::ExtDrawItemListFn Renderer::_extDrawItemList = nullptr;

void Renderer::DrawItemList(Video::Context& context, int groupIndex, int listIndex,
                            int shaderIndex) const {
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/DRAWBATCH.md): ext.draw_batch replaces the whole
    // list walk (consecutive decals / roads / smoke emitters merged into one draw); off, retail's body.
    if (_extDrawItemList != nullptr) {
        _extDrawItemList(*this, context, groupIndex, listIndex, shaderIndex);
        return;
    }
    const RendererDrawGroup& group = _drawGroups._data[groupIndex];
    const Camera& cam = group._cullCamera;

    // ---- the two matrices, built on the stack ------------------------------
    // view = the cull camera's Transform3 expanded to a 4x4 with an explicit
    // (0,0,0,1) fourth column: three zeroed floats and the 1.0f at 0x18016169c.
    Math::Matrix view;
    {
        const Math::Transform3& t = cam._cameraToWorld;
        view.m[0]  = t.axisX.x;       view.m[1]  = t.axisX.y;       view.m[2]  = t.axisX.z;       view.m[3]  = 0.0f;
        view.m[4]  = t.axisY.x;       view.m[5]  = t.axisY.y;       view.m[6]  = t.axisY.z;       view.m[7]  = 0.0f;
        view.m[8]  = t.axisZ.x;       view.m[9]  = t.axisZ.y;       view.m[10] = t.axisZ.z;       view.m[11] = 0.0f;
        view.m[12] = t.translation.x; view.m[13] = t.translation.y; view.m[14] = t.translation.z;
        view.m[15] = 1.0f;
    }
    // proj = the camera's projection matrix, sixteen dwords copied verbatim.
    const Math::Matrix proj = cam._projection;

    context.SetCamera(view, proj);

    // ---- the item list ------------------------------------------------------
    const Collection::Array<RendererDrawItem>& items = group._items[listIndex];

    const int32_t count = items._size;
    if (count <= 0) {
        return;
    }

    int32_t emitted = 0;
    int32_t drawn = 0;
    int64_t i = 0;
    while (i < count) {
        int64_t itemIndex = i;
        const RendererDrawItem& item = items._data[itemIndex];

        int32_t step = 1;
        int32_t batchCount = 1;

        // `item._material` is dereferenced BEFORE the shader-index tests
        // (0x1801054f5 vs 0x1801054f9). Faithful.
        const Material* const material = item._material;
        bool draw = false;

        if (shaderIndex >= 0 && shaderIndex < material->_passes._size) {
            const Video::VideoProgram* const prog = material->_passes._data[shaderIndex].Get();

            const Video::VertexProgram* const vp = prog->_vertexProgram.Get();
            const Video::PixelProgram*  const pp = prog->_pixelProgram.Get();
            if (prog->_vertexLayout.Get() != nullptr &&
                vp != nullptr && vp->_byteCode._size > 0 &&
                pp != nullptr && pp->_byteCode._size > 0) {
                draw = true;
            }
        }

        if (draw) {
            // shader 0 only, and only when the instance has a render texture:
            // `CMP byte [instance+0x24], R15B` with R15B known zero.
            if (shaderIndex == 0 && item._instance->_hasRenderTexture != 0) {
                item._instance->GenerateTextures(context, _frameIndex);
            }
            item._instance->Setup(context, shaderIndex);

            if (item._maxInstances > 1) {
                // ---- the instancing batch ---------------------------------
                // Geometry slot 7 `SetSharedInstanceData`, by name (B3X W0).
                int32_t slotIndex = item._geometry->SetSharedInstanceData(
                    context, item._node, _alphaT);
                const int32_t base = emitted - slotIndex;

                batchCount = 0;
                for (;;) {
                    const RendererDrawItem& cur = items._data[itemIndex];
                    ++batchCount;
                    ++itemIndex;

                    // Geometry slot 6 `GetInstanceTransform`, by name (B3X W0).
                    // Its hidden return slot is `view` -- the stack matrix is
                    // the storage, exactly as the raw call passed it.
                    // XFORMMEMO (NOT RETAIL, graphics/xform_memo.h): the same matrix,
                    // computed once per (instance, list, t) per Renderer::Draw.
                    const Math::Matrix* const m = XformMemo::kEnabled
                        ? XformMemo::Transform(*cur._geometry, cur._node, _alphaT, &view)
                        : cur._geometry->GetInstanceTransform(&view, cur._node, _alphaT);
                    context.SetTransform(slotIndex, *m);

                    ++slotIndex;
                    if (base + slotIndex >= count) break;
                    if (batchCount >= item._maxInstances) break;

                    const RendererDrawItem& nxt = items._data[itemIndex];
                    if (nxt._instance != item._instance) break;
                    if (nxt._node != item._node) break;
                    if (nxt._userData != item._userData) break;
                    if (nxt._animationInstanceId != item._animationInstanceId) break;
                }
                // ESI and R15 are the SAME count in retail (both bumped once
                // per consumed item at 0x1801055c4/0x1801055c6), and both are
                // what the outer counters advance by at 0x1801056b4/0x1801056bd.
                step = batchCount;
            } else {
                // 0x18010569c: a non-instanced item is one item, one instance.
                batchCount = 1;
                step = 1;
            }

            // Geometry slot 2 `Draw`, by name (B3X W0). The material handle is
            // the 8-byte `ExternalPtr` passed by value in its register, as the
            // raw call passed the pointer. LEFT: `ExternalPtrHandle` holds a
            // non-const `void*` (Mesh.h's handle, code/Game's `SetMesh` shape).
            item._geometry->Draw(
                context,
                group._cullCamera,
                ExternalPtrHandle{ const_cast<MaterialInstance*>(item._instance) },  // LEFT: FLOOR? (const_api proposed) retail drops this const itself: ?AddDrawItem@Renderer 0x1801057b0 takes and stores `const MaterialInstance*` (PEBV), and Geometry slot 2 Draw takes ExternalPtr<MaterialInstance> by value (non-const) -- no typing on either side removes it  // CAST-OK: const_api (S4 ruling)
                _alphaT,
                static_cast<int32_t>(item._node),
                shaderIndex,
                batchCount);
            ++drawn;
        }

        emitted += step;
        i += step;
    }
}

}  // namespace Graphics

// ############################################################################
// Graphics::Renderer::DrawItemList
// ?DrawItemList@Renderer@Graphics@@QEBAXAEAVContext@Video@@HHH@Z
// retail 0x180105240, ordinal 2279
// ############################################################################
//
// FIVE VALUES: (this, Context&, int group, int list, int shader). The fifth
// travels in its own home slot at [RBP+0xc0] and is the SHADER INDEX — see
// point 1 of this file's banner, and lights_vtable.cpp's own note about the
// three Draw arms that write their arg5 home slot before tail-jumping here.
//
// SHAPE
//   group = this->_drawGroups._data + group*0x1f0
//   SetCamera(ctx, Matrix(group->cullCamera.cameraToWorld), group->cullCamera.projection)
//   items = group->items[list]                  ; group + 0x1c0 + list*0x10
//   for (i = 0; i < items._size; ) {
//       item = items._data + i*0x38
//       step = 1
//       if (shader >= 0 && shader < item->material->_programCount) {
//           prog = item->material->_programs[shader]
//           if (prog->[0x20] && prog->[0x28] && prog->[0x28]->[0x48] > 0
//                            && prog->[0x30] && prog->[0x30]->[0x48] > 0) {
//               if (shader == 0 && item->instance->hasRenderTexture)
//                   GenerateTextures(item->instance, ctx, this->contextSerial)
//               MaterialInstance::Setup(item->instance, ctx, shader)
//               if (item->_maxInstances > 1) { ...the instancing batch, below... }
//               item->geometry->vt[2](geometry, ctx, &group->cullCamera,
//                                     item->instance, this->frameAlpha,
//                                     item->arg5, shader, batchCount)
//           }
//       }
//       emitted += step; i += step
//   }
//
// THE FIVE SILENT DROPS are AddDrawItem's own list (wave9_lane_gfx.cpp) read
// from the other end: a negative or out-of-range shader index, and a selected
// VideoProgram with a null +0x20, a null-or-empty +0x28, or a null-or-empty
// +0x30. On every one of them `step` stays 1 and nothing is drawn.
//
// THE INSTANCING BATCH (`item->_maxInstances > 1`) is the part no decompile shows
// honestly. It calls the geometry's slot 7 to reserve N instance transform
// slots, then walks forward over CONSECUTIVE items that agree on all four of
// (instance, arg5, userData, arg10), asking each one's slot 6 for a Matrix and
// pushing it with Context::SetTransform. It stops on the first mismatch, on
// `item->_maxInstances` items, or when the running index would pass the list's size.
// `step` is then the number of items consumed, so the outer loop skips the
// whole batch and the single Draw below carries them all.
//
// ORACLE: NONE, and that is stated rather than faked. Every arm of this body
// ends in a real GPU draw through the geometry's vtable; running the shipped
// copy afterwards would submit every draw a second time. the diff oracle records
// how many calls reached a Draw at all, which is the number that matters — a
// green `calls=` column on a body that dropped every item would mean nothing.
// ############################################################################
