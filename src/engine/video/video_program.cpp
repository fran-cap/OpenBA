// ============================================================================
// src/engine/video/video_program.cpp — Wave 23 lane VIDVT
//
//   ?SetupShaders@VideoProgram@Video@@QEBAXAEAVContext@2@@Z   retail 0x1800f4520
//
// The ONE body row in lane VIDVT's nineteen. It entered `called` in Wave 21:
// lane GFXBODY's port of `?Setup@MaterialInstance@Graphics@@` calls it by name
// (src/engine/graphics/wave21_lane_gfxbody.cpp), which grew the cutover
// surface by one row (reference_porting_can_grow_the_cutover_surface.md).
// GFXBODY's own §6 left it as a residual in these words: "it is [safe to leave
// on the DLL], today, because that is where it already was — but this lane made
// our tree depend on it by name for the first time and did not read its body."
// This is that read, and the port.
//
// ---------------------------------------------------------------------------
// EXTENT — 122 bytes, 0x1800f4520 .. 0x1800f459b inclusive, and note HOW
// ---------------------------------------------------------------------------
// NOT from `.pdata`: this function has NO `.pdata` entry at all. It is a
// frameless leaf — no prologue, no unwind data — so the nearest PRECEDING
// RUNTIME_FUNCTION belongs to `?PlatformDestruct@RenderBuffer@Video@@`
// (0x1800f4400..0x1800f44de) and a tool that sizes by "the covering .pdata
// entry" without checking containment would silently hand back a DIFFERENT
// function's 222 bytes. (reference_pdata_is_a_lower_bound_on_function_extent.md
// is the related trap in the other direction — chained fragments — and does not
// arise here because there is no entry to chain.)
//
// The extent used is instead the ADJACENT-EXPORT upper bound (next export
// 0x1800f45b0) NARROWED by the decode: a `repz ret` at 0x1800f459a followed by
// 0x14 bytes of `int3` alignment padding. The upper bound and the decode agree
// to the padding, so 122 is exact rather than an upper bound
// (reference_adjacent_export_is_an_upper_bound.md).
//
// ---------------------------------------------------------------------------
// THE BODY, read as disassembly and not as decompiled C
// ---------------------------------------------------------------------------
//   RCX = this (VideoProgram const*),  RDX = ctx (Context&)  -- QEBAX...AEAV
//
//   1800f4520  cmp   qword ptr [rdx + 0x200], rcx
//   1800f4527  je    1800f4537
//   1800f4529  or    dword ptr [rdx + 0x10], 0x100
//   1800f4530  mov   qword ptr [rdx + 0x200], rcx
//   1800f4537  mov   rax, qword ptr [rcx + 0x20]
//   1800f453b  cmp   rax, qword ptr [rdx + 0xb0]
//   1800f4542  je    1800f454f
//   1800f4544  or    dword ptr [rdx + 0x10], 4
//   1800f4548  mov   qword ptr [rdx + 0xb0], rax
//   1800f454f  mov   rax, qword ptr [rcx + 0x28]      ; and the same shape for
//   ...                                               ; 0x1f0 / 0x1f8 / 0x1c8
//   1800f459a  repz ret
//
// Five compare-then-OR-then-store pairs, no calls, no allocation, no loop, and
// EVERY memory operand in the function is one of exactly ten: four reads of
// `this` (+0x20 +0x28 +0x30 +0x38) and six accesses of `ctx` (+0x10 +0xb0
// +0x1c8 +0x1f0 +0x1f8 +0x200). That enumeration is what makes the oracle below
// provably complete rather than merely plausible.
//
// WHAT IT IS. It is the five `?Set*@Context@Video@@` setters inlined into one
// call, and the destination/bit pairs line up one-for-one with the dirty-mask
// table already in src/engine/video/Context.h:
//
//   ctx+0x200 <- this          bit 0x100  _videoProgram   (?SetVideoProgram@)
//   ctx+0x0b0 <- this+0x20     bit 0x004  _vertexLayout   (?SetVertexLayout@)
//   ctx+0x1f0 <- this+0x28     bit 0x040  _vertexProgram  (?SetVertexProgram@)
//   ctx+0x1f8 <- this+0x30     bit 0x080  _pixelProgram   (?SetPixelProgram@)
//   ctx+0x1c8 <- this+0x38     bit 0x020  _renderState    (?SetRenderState@)
//
// That agreement is the evidence for the VideoProgram layout below: the four
// fields are identified by WHICH CONTEXT SLOT each is copied into and WHICH
// DIRTY BIT is raised for it, not by a reader's guess. `WAVE6_LANE_F.md` §117
// had already established that `+0x20` on this object is non-null-checked by a
// caller that hands the object to `SetupShaders`; this pins its type.
//
// Retail's compare for `_vertexLayout` is spelled `cmp rax, [rdx+0xb0]` where
// the other four are `cmp [rdx+off], rax` / `cmp [rdx+off], rcx`. Operand order
// in a CMP changes only the flags' sign, and every one of the five branches on
// ZF alone, so this is a codegen artefact and not a semantic difference. It is
// recorded rather than smoothed over.
//
// ---------------------------------------------------------------------------
// THE ORACLE, and why RESTORE-AND-REPLAY is sound here specifically
// ---------------------------------------------------------------------------
// A naive replay is a TAUTOLOGY for this function: it is compare-guarded, so
// running retail's body AFTER ours on the same Context finds all five fields
// already equal and writes nothing, and the comparison passes no matter what
// ours did. WAVE9_LANE_FLUSH.md §215 records exactly that hazard for this
// family of bodies.
//
// So: snapshot the six ctx words, run OURS, capture ours' six, restore the six
// to the pre-state, run RETAIL's on the live Context, and compare. That is
// complete because the ten-operand enumeration above proves the function reads
// and writes NOTHING ELSE — a restore of those six words restores the entire
// input state this function can observe. The live Context ends carrying
// retail's values, which are the same values when the port is right.
//
// It is also NOT reading bytes neither side writes
// (reference_oracle_must_not_compare_unwritten_bytes.md): all six compared
// words are written by at least one arm of the function, and the dirty mask is
// compared as a whole because retail ORs into whatever was already there.
//
// `self` is `const` to retail and is not snapshotted; nothing aliases it to
// `ctx` (one is a VideoProgram, the other a 0x2168-byte Context, and the
// caller in wave21_lane_gfxbody.cpp passes two distinct objects), so
// reference_oracle_must_snapshot_aliased_inputs.md's trap does not arise. That
// is asserted from the call site, not assumed from the types.
// ============================================================================

// ============================================================================
// ENDGAME B.3.5 BLOCK B — THE BODY MOVED INTO THE MEMBER
// ============================================================================
// `?SetupShaders@VideoProgram@Video@@` is now
// `Video::VideoProgram::SetupShaders(void*)`, a member of the real class in the
// new src/engine/video/VideoProgram.h, and the registered door is a one-line
// QUALIFIED non-virtual delegation through a `reinterpret_cast` bridge.
//
// THE LOCAL `struct VideoProgramPartial` IS GONE. It pinned four offsets of a
// class that had no header anywhere in the tree, with no `sizeof` assert because
// the rest was unknown; the header now pins all five and asserts the 0x48, and the
// four offsets this file proved — by WHICH Context slot each field feeds and WHICH
// dirty bit it raises — are cited there as one of its four independent agreements.
//
// THE ORACLE SCAFFOLDING IS GONE TOO, and that is a warning fix, not a loss.
// `Capture`, `Restore` and the unused `pre` were the restore-and-replay harness
// described below; the replay call itself was removed when the port closed, leaving
// three functions and a variable that nothing used — three warnings in both builds,
// on a file this block touches. The METHOD stays written down here, which is the
// part that has to survive.
//
// `Video::Context` is block D1's class and is still the 0x2168-byte POD
// `RtVideo::Context` today, so the member takes a `void*` and casts. Inside
// `namespace Video` the POD is spelled with its `RtVideo::` qualifier, as
// B34_BRIEFS A.7 requires now that this file opens that namespace: a bare
// `Context` there would bind to a class.
// ============================================================================

#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "video/Context.h"
#include "video/VideoProgram.h"

namespace {

// One compare-then-OR-then-store, retail's idiom, for a typed pointer slot of
// the Context (B.4 R-VIDEO: the four slots are typed fields now).
template <class T>
inline void Apply(Video::Context* ctx, const T*& slot, const T* value, uint32_t bit) {
    if (slot != value) {
        ctx->_dirty |= bit;
        slot = value;
    }
}

// ----------------------------------------------------------------------------
// The registered door. Kept inside the anonymous namespace: nothing outside this
// file needs the symbol -- the registry takes its ADDRESS, not its name.
// ----------------------------------------------------------------------------
}  // namespace

// ============================================================================
// THE MEMBER BODY — `Video::VideoProgram::SetupShaders`. At `namespace Video`
// scope and AFTER the anonymous namespace (B34_BRIEFS A.2); `Apply` stays
// reachable by unqualified name.
// ============================================================================
namespace Video {

void VideoProgram::SetupShaders(Context& context) const {
    Context* ctx = &context;

    // Context+0x200 is `_videoProgram`, the first member of
    // Context::PixelConstantStaging (src/engine/video/Context.h).
    if (ctx->_pixelConstantStaging.videoProgram != this) {
        ctx->_dirty |= RtVideo::kDirtyVideoProgram;
        ctx->_pixelConstantStaging.videoProgram = this;
    }

    Apply(ctx, ctx->_vertexLayout,  _vertexLayout.Get(),  RtVideo::kDirtyVertexLayout);
    Apply(ctx, ctx->_vertexProgram, _vertexProgram.Get(), RtVideo::kDirtyVertexProgram);
    Apply(ctx, ctx->_pixelProgram,  _pixelProgram.Get(),  RtVideo::kDirtyPixelProgram);
    Apply(ctx, ctx->_renderState,   _renderState.Get(),   RtVideo::kDirtyRenderState);
}

}  // namespace Video
