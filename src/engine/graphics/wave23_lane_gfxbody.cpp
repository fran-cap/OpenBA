// ============================================================================
// src/engine/graphics/wave23_lane_gfxbody.cpp — Wave 23 lane GFXBODY
// ============================================================================
//
// Five of the ten Graphics function rows this lane was dealt. The triage for
// all ten — including the five NOT ported here and why — is
// docs/history/runtime_port/WAVE23_LANE_GFXBODY.md. Read §5 before adding a sixth.
//
//   | Export                                        | Retail      | Ord  | Bytes |
//   |-----------------------------------------------|-------------|------|-------|
//   | ??0FrameCopyImageBuffer@Graphics@@QEAA@XZ     | 0x1800a3d10 |  198 |   304 |
//   | ??1FrameCopyImageBuffer@Graphics@@UEAA@XZ     | 0x1800a3c80 |  597 |   138 |
//   | ?Create@FrameCopyImageBuffer@Graphics@@UEAA_N…| 0x1800a3b70 | 1877 |   197 |
//   | ?Generate@FrameCopyImageBuffer@Graphics@@UEBAX…| 0x1800a3b20 | 2395 |   60 |
//   | ?Update@AnimationManager@Graphics@@QEAAX…     | 0x1800fc240 | 4473 |   430 |
//
// Every name and address above is `pefile`'s reading of the export directory of
// the shipped C:\GOG Games\Banished\Runtime-x64.dll, run before a line of this
// file was written (reference_resolveexport_phantom_names.md). That check also
// settled the dtor's spelling, which the lane brief flagged as a trap: the
// `UEAA` form IS in the directory at 0x1800a3c80 and the non-virtual `QEAA`
// form is ***ABSENT*** — a `??1FrameCopyImageBuffer@Graphics@@QEAA@XZ`
// registration would have resolved NULL and registered nothing, silently
// (reference_phantom_dtor_mangling_qeaa_vs_ueaa.md).
//
// EVERY BODY HERE WAS TRANSCRIBED FROM THE DISASSEMBLY, with Ghidra's C read
// afterwards as a second, differently-biased opinion. That order mattered twice
// in this file:
//
//   * the ctor's decompile invents `extraout_RAX` for the inlined operator-new
//     result and makes the null-allocation path look like it stores an
//     uninitialised value. The bytes say `MOV RAX,RBP` (RBP == 0) — see §1.
//   * `AnimationManager::Update`'s decompile is not consulted at all for the
//     argument slots; the nine-argument callee is spelled out in full by its
//     own exported mangled name, which is a stronger source than either.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "system/MemoryAllocator.h"
#include "graphics/AnimationManager.h"   // ENDGAME B.3.5 block GC
#include "graphics/AnimationState.h"     // ditto
#include "graphics/FrameCopyImageBuffer.h"
#include "math/Math.h"                   // Math::Transform / Vector3
#include "system/Value.h"                // System::Time
#include "video/Device.h"                // ENDGAME B.3.5: Video::RenderBuffer
#include "video/ImageBuffer.h"           // ENDGAME B.3.5: Video::ImageBuffer

// ---------------------------------------------------------------------------
// THE NEGATIVE-CONTROL SEAM IS GONE -- ENDGAME B.3.5 block GC.
//
// This file used to carry a `GFXBODY23_PROBE` / `RT_GFX23_CORRUPT(n)` seam that
// wrapped seven sites inside `AnimationManager::Update` so a probe binary could
// corrupt one of them at a time. THE PROBE IT WAS WRITTEN FOR DOES NOT EXIST:
// a repo-wide search for `GFXBODY23_PROBE`, `gfxbody23` and
// `GfxbodyProbeCorrupt` finds nothing outside this file except one stale
// sentence in AnimationManager.h -- no `tools/gfxbody23_probe`, no Makefile
// target. So every guarded site expanded to `(false)` in every build this tree
// can produce, and the seam documented a measurement nobody can re-run.
//
// It is deleted with the body's move onto `Graphics::AnimationManager::Update`,
// which is the block brief's "may be deleted if and only if nothing references
// it" arm; the seven sites are now their live arms, written plainly. THE
// ATTENDANCE COUNTERS ARE A DIFFERENT THING: `g_animUpdStates` has a live
// reader in `RtGraphicsWave23GfxbodySummary()`. (`g_animUpdCalls` was the
// entry count of the unreferenced `RtGraphics_AnimationManager_Update` door and
// went with it in B.4 R0: it could only ever print 0.)
// ---------------------------------------------------------------------------


namespace {

// ---------------------------------------------------------------------------
// ATTENDANCE. Same shape and the same reasoning as wave21_lane_gfxbody.cpp:125:
// a port with no attendance counter cannot be told apart from an absent one,
// and `calls=0` with `worked>0` is a different finding from `never entered`
// (reference_calls_zero_can_mean_always_took_the_uncovered_branch.md). Printed
// at 1, 10, 1000 and then every 100000, so a log tail carries the evidence.
//
// `g_generateCopies` is deliberately NOT an entry counter: Generate's whole
// body is behind one equality test, so an entry count of N says nothing about
// whether the copy ever happened. It counts the times the latch DIFFERED and
// the blit was actually issued — the branch, not the door.
// ---------------------------------------------------------------------------
void Attend(const char* what, unsigned long long& n) {
    ++n;
    if (n == 1 || n == 10 || n == 1000 || (n % 100000) == 0)
        printf("[GFXBODY23] %s calls=%llu\n", what, n);
}

unsigned long long g_ctorCalls      = 0;
unsigned long long g_dtorCalls      = 0;
unsigned long long g_createCalls    = 0;
unsigned long long g_generateCalls  = 0;
unsigned long long g_generateCopies = 0;   // the BRANCH, not the door
unsigned long long g_animUpdStates  = 0;   // inner-loop iterations; 0 == "ran, did nothing"

// ?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z, retail 0x1800c7a50.
//
// The ctor and Create do NOT call operator new; they INLINE it — the same five
// instructions (`cs = *0x1801fc718 + 0x640; Enter; Allocate(singleton, n, 8);
// Leave`) that wave21_lane_gfxbody.cpp:184 disassembled and showed to be
// instruction-for-instruction `MemAlloc(n, 8)`, with the alignment arriving in
// EDX instead of an immediate. Routing through MemAlloc is therefore not an
// approximation of the inlined sequence, it is that sequence; and it keeps the
// arena identical whether or not lane ALLOC's port is armed. B3R: it is
// System::MemoryAllocator::MemAlloc, called directly.

// ---------------------------------------------------------------------------
// The seven named dependencies. ALL SEVEN ARE ALREADY `called` AND `ported=1`
// in today's census, so this file adds NOTHING to the cutover surface on the
// FrameCopyImageBuffer side (reference_porting_can_grow_the_cutover_surface.md
// is the hazard; here it is measured and it is zero). The one place this lane
// DOES grow the surface is AnimationState::Update — see its own note below.
//
// Called by name (B3R).
// ---------------------------------------------------------------------------

// ENDGAME B.3.9 VS: `::new (p) Video::ImageBuffer()` -- the ??0 door and the minted row are retired.
void ImageBufferCtor(void* p) {
    ::new (p) Video::ImageBuffer();
}
// (ENDGAME B.3.9 GFX: `ImageBufferDtor` went with the ??1 door; the real
// ~FrameCopyImageBuffer chains ~ImageBuffer itself.)
// ENDGAME B.3.5 caller batch: `PlatformConstruct`, `PlatformDestruct` and
// `Create` are DEFINED members of the real `Video::RenderBuffer` today, so the
// three by-name doors become member calls and `RbCreateFn` goes with them. The
// `??3` operator delete below is the class member too (B3R).
void RenderBufferPlatformConstruct(Video::RenderBuffer* p) {
    p->PlatformConstruct();
}
void RenderBufferPlatformDestruct(Video::RenderBuffer* p) {
    p->PlatformDestruct();
}
void RenderBufferOperatorDelete(void* p) {
    Video::RenderBuffer::operator delete(p);       // 0x18001f360
}
bool RenderBufferCreate(Video::RenderBuffer* rb, const Video::ImageBuffer* colour,
                        const Video::ImageBuffer* depth) {
    return rb->Create(colour, depth);
}

// The three zeroed qwords retail stores into a fresh RenderBuffer before
// PlatformConstruct: +0x00 / +0x08 (the two attachments) and +0x10 (width,
// height).
void RenderBufferZeroHeader(Video::RenderBuffer* rb) {
    rb->colorBuffer = nullptr;
    rb->depthBuffer = nullptr;
    rb->width  = 0;
    rb->height = 0;
}

// ENDGAME B.3.9 block GFX: `VtFrameCopy` / `VtGenerated` (the two minted
// tables' rebased addresses) are gone with the ??0 / ??1 doors that stamped them.

}  // namespace

// ============================================================================
// §1.  ??0FrameCopyImageBuffer@Graphics@@QEAA@XZ   retail 0x1800a3d10, ord 198
// ============================================================================
// 304 bytes / 80 instructions. Constructs the ImageBuffer base, then builds the
// two members the class owns unconditionally: a second ImageBuffer aliased onto
// the device back buffer, and a RenderBuffer pointing at it.
//
// *** THE ONE PLACE THE DECOMPILER WOULD HAVE MISLED A REWRITE. ***
// Ghidra renders the inlined operator new as
//
//     pRVar1 = this_01;                       // this_01 == 0
//     if (this_00 != 0) { ImageBuffer::ImageBuffer(this_00); pRVar1 = extraout_RAX; }
//     *(RenderBuffer **)(this + 0x70) = pRVar1;
//
// which reads as "store the constructor's return value". The bytes at
// 0x1800a3d9f-0x1800a3dae are
//
//     JZ  0x1800a3dab                          ; allocation failed
//     MOV RCX,RDI ; CALL 0x1800f2580 ; JMP 0x1800a3dae
//   0x1800a3dab:
//     MOV RAX,RBP                              ; RBP is 0 for the whole body
//   0x1800a3dae:
//     MOV qword ptr [RSI + 0x70],RAX
//
// so RAX is the ALLOCATION on both paths — `this` returned in RAX by MSVC
// convention on one, and the null RBP on the other. We store `image`, never a
// constructor's return, which is the rule
// src/engine/save/saveload_draw.cpp:427 and
// src/engine/graphics/sunlight.cpp:230 both paid for: this tree's ported
// constructors are not uniform about returning `this`, and one of them
// (`Video_ImageBuffer_Ctor`, buffers.cpp:387) is declared `void`. Using the
// pointer we already hold is byte-faithful to what retail computes AND
// independent of how any port spells its signature.
//
// THE UNGUARDED DEREF IS DELIBERATE AND FAITHFUL. If MemAlloc returns null,
// retail walks straight into `MOV RCX,[RAX+0x60]` and dies. Per CLAUDE.md
// ("don't fix a faithful crash with a null check") that is reproduced, not
// guarded: the deref is load-bearing — retail relies on the allocation being
// there, and skipping the work would not restore the behaviour.
//
// Retail ends `MOV RAX,RSI; RET`, i.e. it returns `this`; every C++ builder
// holds the pointer it constructed into, so nothing reads that RAX any more.
//
// ENDGAME B.3.9 block GFX: the ??0 door that stood here is RETIRED. The real
// `FrameCopyImageBuffer()` is the constructor: ImageBuffer() (0x1800a3d2d), the
// Generated stamp and `_generation = 0` (GeneratedImageBuffer()) and the
// FrameCopy stamp (0x1800a3d49) are the compiler's chain; its body (below, §2's
// end) is everything after the FrameCopy stamp. Every builder -- the factory
// pair, Construct / Reconstruct, SaveLoad::Draw and the exe's TakeScreenshot --
// spells `::new (p) Graphics::FrameCopyImageBuffer()`.
// ============================================================================

// ============================================================================
// §2.  ??1FrameCopyImageBuffer@Graphics@@UEAA@XZ   retail 0x1800a3c80, ord 597
// ============================================================================
// 138 bytes / 37 instructions. Mirror of the ctor, plus the base-class handback.
//
// The `UEAA` spelling is the one that exists (see banner). This is the SCALAR
// destructor; vtable slot 0 is the separate deleting destructor (0x18001f260),
// which is not this row and is not ported here.
//
// The member at +0x70 is destroyed through ITS OWN vtable slot 0 with EDX=1 —
// a DELETING destructor call, so it both destructs and frees. The two
// RenderBuffers are destroyed the long way (`PlatformDestruct` then
// `operator delete`) because RenderBuffer has no virtual destructor to reach.
// Those are not the same idiom and swapping one for the other would leak or
// double-free (reference_a_written_recipe_can_be_wrong.md is the general form).
//
// ENDGAME B.3.9 block GFX: the ??1 door that stood here is RETIRED with the
// RT_DELDTOR row 0x18001f260 it served. The restamp (0x1800a3c9b), the Generated
// restamp (0x1800a3cee) and the tail JMP ~ImageBuffer are the compiler's chain
// around the real `~FrameCopyImageBuffer()` below.
// ============================================================================

// ============================================================================
// Graphics::FrameCopyImageBuffer::FrameCopyImageBuffer / ~FrameCopyImageBuffer:
// 0x1800a3d10 after the FrameCopy stamp, and 0x1800a3c80 between its restamp
// and the Generated restamp -- the bodies the B.3.8 `InitFields` /
// `ReleaseFields` split shared with the retired doors, MOVED VERBATIM through
// the field-identical `RtGraphics::FrameCopyImageBuffer` POD view. B.4: on
// the real class's own fields.
// ============================================================================
Graphics::FrameCopyImageBuffer::FrameCopyImageBuffer() {
    Attend("FrameCopyImageBuffer::ctor", g_ctorCalls);
    _backBufferImage = nullptr;                      // 0x1800a3d4c
    _sourceTarget    = nullptr;                      // 0x1800a3d50
    _destTarget      = nullptr;                      // 0x1800a3d54

    // --- the back-buffer alias ---------------------------------------------
    // The raw 0x68-byte block, typed once as the ImageBuffer built in it.
    Video::ImageBuffer* const image = static_cast<Video::ImageBuffer*>(
        System::MemoryAllocator::MemAlloc(0x68, 8));                          // LEA EDX,[RBP+0x68] / LEA R8D,[RBP+8]
    if (image != nullptr) ImageBufferCtor(image);
    _backBufferImage = image;                        // never the ctor's return; see banner

    // `MOV RCX,[RAX+0x60]; MOV RAX,[RCX]; CALL [RAX+8]` — slot 1 of the
    // ImageBuffer's platform companion, `this` and nothing else (retail sets no
    // other argument register): `ImageBufferData::AttachColorBuffer`.
    // src/render/dx11/imagebufferdata.cpp:11 names slot 1 "alias the DeviceData color
    // buffer, build an RTV", which is what makes this member the back buffer
    // rather than a fresh texture.
    image->_data->AttachColorBuffer();               // faithful: unguarded

    // --- the RenderBuffer over it -------------------------------------------
    Video::RenderBuffer* const rb = static_cast<Video::RenderBuffer*>(
        System::MemoryAllocator::MemAlloc(0x30, 8));                          // MOV EDX,0x30 / LEA R8D,[RDX-0x28]
    if (rb != nullptr) {
        // Three qword zero stores INSIDE the success arm, then PlatformConstruct
        // — the same shape saveload_draw.cpp:497 transcribed for the standalone
        // RenderBuffer it builds.
        RenderBufferZeroHeader(rb);
        RenderBufferPlatformConstruct(rb);
    }
    _sourceTarget = rb;

    RenderBufferCreate(rb, _backBufferImage, nullptr);   // XOR R8D,R8D => depth = null
}

Graphics::FrameCopyImageBuffer::~FrameCopyImageBuffer() {
    Attend("FrameCopyImageBuffer::dtor", g_dtorCalls);

    if (_sourceTarget != nullptr) {                  // +0x78
        RenderBufferPlatformDestruct(_sourceTarget);
        RenderBufferOperatorDelete(_sourceTarget);
    }
    if (_destTarget != nullptr) {                    // +0x80
        RenderBufferPlatformDestruct(_destTarget);
        RenderBufferOperatorDelete(_destTarget);
    }
    if (_backBufferImage != nullptr) {               // +0x70
        // Retail: slot 0 with EDX = 1 -> the deleting destructor frees as well
        // as destructs. `Video::ImageBuffer*`: `delete` is that call.
        delete _backBufferImage;
    }
}

// ============================================================================
// §3.  ?Create@FrameCopyImageBuffer@Graphics@@UEAA_N…  retail 0x1800a3b70, 1877
// ============================================================================
// 197 bytes / 54 instructions. ELEVEN parameters plus `this`, and the order is
// read from the export table's own mangled name, not guessed from the call
// site — `_N Create(ImageType, Usage, Format, unsigned int, int, int,
// FilterMode, AddressMode, AddressMode, AddressMode,
// Collection::Array<Collection::Array<uchar>>*)`. saveload_draw.cpp:463 typed
// its vtable call from the same reading, independently.
//
// *** THE STORES ARE NOT IN PARAMETER ORDER. *** Retail writes
// +0x28/+0x2c/+0x30 from the three register arguments, then +0x20 and +0x24
// from the FIFTH and SIXTH stack arguments, then +0x34 from the FOURTH. A
// transcription that assumed "stack args land in ascending field order" would
// put the flags word into `width`. The mapping below is instruction-by-
// instruction from 0x1800a3b87-0x1800a3bc9, cross-checked against the field
// names src/engine/video/ImageBuffer.h recovered from the DX11 side.
// ============================================================================
// ENDGAME B.3.5 block GC: THE BODY MOVED onto
// `Graphics::FrameCopyImageBuffer::Create` -- vtable SLOT 9, which this class
// OVERRIDES (the header's correction to B35_RULES 2.3 item 5). ENDGAME B.3.9
// GFX: the name door is retired with the minted table (its only reader); the
// entry counter moved into the member.

// ============================================================================
// §4.  ?Generate@FrameCopyImageBuffer@Graphics@@UEBAX…  retail 0x1800a3b20, 2395
// ============================================================================
// 60 bytes / 18 instructions, and `UEBA` — const. The whole body is behind one
// integer comparison, which is why the counter below counts the BRANCH and not
// the entry (reference_calls_zero_can_mean_always_took_the_uncovered_branch.md:
// an entry count here is exactly the number that cannot tell you whether the
// copy happened).
//
// THREE arguments at the indirect call, not two. `MOV R9B,0x1` at 0x1800a3b41
// sets a fourth register before `CALL [RAX+0x18]`, so the companion's slot 3
// takes (this, Context*, RenderBuffer*, bool). saveload_draw.cpp:75 records the
// same class of defect on the call INTO this function, found the same way.
// ============================================================================
// ENDGAME B.3.5 block GC: THE BODY MOVED onto
// `Graphics::FrameCopyImageBuffer::Generate` -- vtable slot 10. ENDGAME B.3.9
// GFX: the name door is retired with the minted table (its only reader); the
// ENTRY counter moved into the member beside the BLIT counter.

// ============================================================================
// §5.  ?Update@AnimationManager@Graphics@@QEAAXAEBVTime@System@@_N@Z
//      retail 0x1800fc240, ordinal 4473. 430 bytes / 104 instructions, ONE call.
// ============================================================================
// THE INHERITED HOLDBACK NAMED THE WRONG HAZARD, AND THIS LANE CHECKED BOTH
// HALVES OF IT (rule 3). src/engine/graphics/animation.cpp:70-79 held this
// back on "a wrong argument-slot mapping in the 9-argument
// AnimationState::Update call … through a nested, UNEXPORTED AnimationState
// record". Wave 21 already showed the callee IS exported; this lane confirms it
// with pefile — 0x1800fdb20, ordinal 4474 — and its mangled name spells all
// eight parameters and their types, so the named risk is answerable from the
// export table with no reversing at all. The residual risk is the ITERATION
// LAYOUT, which is what §5.1 pins.
//
// The SECOND half of that holdback ("it runs behind a default-on non-retail
// batching path, src/ext/render_culling/render_culling.cpp ANIM_BATCH_ALL") was checked
// too and is NOT a blocker on this body: that path decides which MeshInstances
// REGISTER a group with the manager, i.e. it changes this function's INPUT, not
// its contract. A manager with no groups makes this body a single compare and a
// return — which is exactly why `g_animUpdStates` exists.
//
// THIS ROW GROWS THE CUTOVER SURFACE BY ONE, and it is the only place in this
// file that does. `?Update@AnimationState@Graphics@@…` appears NOWHERE in the
// census today — not in `called`, not in `ported`. Porting this body puts it
// there, unported (reference_porting_can_grow_the_cutover_surface.md). Said out
// loud because the census prints the new row without saying who added it.
//
// ---------------------------------------------------------------------------
// §5.1  THE TWO RECORDS, AND A CORRECTION TO src/engine/graphics/Animation.h
// ---------------------------------------------------------------------------
// `AnimationManager` +0x00/+0x08/+0x0c are a `Collection::Array<Group>`
// (_data / _size / _capacity), NOT the CRITICAL_SECTION-shaped triple that
// header currently describes. Proof, from this body alone: 0x1800fc268 reads
// `*(int*)(this+8)` as a LOOP BOUND and 0x1800fc2a0 reads `*(void**)this` as
// the base of an array of 0x30-byte records. A LockCount is not a loop bound.
// The header's own dtor note gives the second half of the proof once read the
// other way round: it says ~AnimationManager calls FUN_18005bf60 "as if `this`
// were an LPCRITICAL_SECTION (it reads ->DebugInfo, ->LockCount,
// ->RecursionCount at exactly +0x00/+0x08/+0x0c)". Those three offsets are
// _data / _size / _capacity, and FUN_18005bf60 destroys each element, frees
// _data, then zeroes all three — it is `Collection::Array<Group>::~Array`.
// Ghidra typed its parameter LPCRITICAL_SECTION only because it frees under the
// allocator's own critical section. The header is corrected in this branch; the
// ported ctor and dtor in animation.cpp needed no behavioural change, because
// they already delegate to the real helper at its retail address.
//
//   Group, stride 0x30      +0x00  pointer, null => skip the whole group
//                           +0x10  Collection::Array<GraphicsMesh::Node>, passed BY REFERENCE
//                           +0x20  AnimationState array base
//                           +0x28  int state count
//
//   AnimationState, stride 0x48
//                           +0x18  Math::Vector3   (written by the callee; read back here)
//                           +0x28  InterpolatedState<Transform>*   (6th arg, and the write target)
//                           +0x38  InterpolatedState<Transform>*   (5th arg)
//
// Wave 21 §4.3 enumerated the same offsets from Ghidra's C and explicitly
// recorded that they had NOT been checked against the disassembly. They now
// have been, and they hold — but the check was worth doing, because the SIGN of
// such an agreement is not informative until someone runs it
// (reference_ghidra_c_output_vs_disassembly.md).
//
// ---------------------------------------------------------------------------
// §5.2  THE IDENTITY TRANSFORM IS A CONSTANT, NOT A COMPUTATION
// ---------------------------------------------------------------------------
// The 4th argument is a stack `Math::Transform` rebuilt every iteration as
// {0,0,0, 1.0f, 0,0,0}: quaternion identity then a zero Vector3. The 1.0f comes
// from `MOVSS XMM7,[0x18016169c]`, and the four bytes there were READ
// (00 00 80 3f) rather than assumed — a float constant lifted by eye is how
// reference_crt_coefficients_must_be_lifted_not_retyped.md got written.
//
// ---------------------------------------------------------------------------
// §5.3  WHAT RETAIL DOES AFTER THE CALL, AND WHY IT IS AN INT COPY
// ---------------------------------------------------------------------------
// It writes a Transform into `*state->_interp28`: identity quaternion, then the
// Vector3 the callee just left at `state+0x18`, moved as THREE DWORDS
// (`MOV ECX,[RBX]` / `MOV EDX,[RBX+4]` / `MOV R8D,[RBX+8]` then three
// `MOV dword`). Copied as uint32 here for the same reason
// wave21_lane_gfxbody.cpp gives for its four constants: an integer move of a
// float is bit-exact where a float assignment is only value-exact, and the two
// differ on a signalling NaN or a negative zero.
//
// ---------------------------------------------------------------------------
// §5.4  THE BOUNDS ARE RE-READ EVERY ITERATION, AND THAT IS TRANSCRIBED
// ---------------------------------------------------------------------------
// Both loops reload their limit from memory at the bottom (`CMP R12D,
// [RSI+RBP+0x28]`, `CMP R14D,[R15+8]`) and the outer loop reloads `_data` at
// the top. The callee could in principle grow either array, so this is not
// merely stylistic — it is written unhoisted because that is what the bytes do.
// ============================================================================
namespace {

// B.4: the file-local `AnimGroup` (stride 0x30) and `AnimTransform` PODs are
// gone. The group is `Graphics::AnimationManager::Instance` (`_group` +0x00,
// `_nodes` +0x10, `_states` +0x20 / count +0x28, AnimationManager.h's asserts)
// and the transform is `Math::Transform`.

// The nine-argument `RT_EXPORT_ONCE` forwarder onto
// `?Update@AnimationState@Graphics@@` that used to sit here is GONE: block GC
// made that a real member of `Graphics::AnimationState`, so the call site is a
// QUALIFIED member call. The mangled name -- which is what settled the argument
// mapping, and is a stronger source than any prologue read -- is kept at the
// call site.

}  // namespace

// One line beside the other wave summaries, so a run that entered none of these
// says so rather than staying silent.
void RtGraphicsWave23GfxbodySummary() {
    printf("[GFXBODY23] ctor=%llu dtor=%llu Create=%llu Generate=%llu (blits=%llu) "
           "AnimUpdate states=%llu\n",
           g_ctorCalls, g_dtorCalls, g_createCalls, g_generateCalls, g_generateCopies,
           g_animUpdStates);
}

// ############################################################################
// ENDGAME B.3.5 step 5, block GC -- `Graphics::AnimationManager::Update`'s body,
// whose live door is the `extern "C"` function above.
//
// Its own `namespace Graphics { }` block, because a member definition has to
// appear in a namespace ENCLOSING the class and `Graphics` / `RtGraphics` are
// siblings (B34_BRIEFS A.2). This file's helpers are in the GLOBAL unnamed
// namespace and are spelled `::` here, because inside `namespace Graphics` a
// bare `AnimGroup` or `AnimTransform` would look inside `Graphics` first.
// ############################################################################

namespace Graphics {

// ---------------------------------------------------------------------------
// ?Update@AnimationManager@Graphics@@QEAAXAEBVTime@System@@_N@Z 0x1800fc240
// ENDGAME B.3.5 block GC. The group/state offsets, the identity-transform
// constant, the integer copy and the re-read bounds are all in the banner beside
// the door above.
//
// `_instances` is the `Array<Instance>` at +0x00, so retail's `[RCX]` and
// `[RCX+8]` are `_instances._data` and `_instances._size` -- the same two loads,
// now held by AnimationManager.h's own `static_assert`s. The GROUP record is
// still this file's `AnimGroup` POD: it is field-for-field
// `Graphics::AnimationManager::Instance` and its four asserts stay, because the
// POD has live users outside this file and B4b-1 keeps it until the last one
// converts.
//
// THE CALLEE EDGE IS NO LONGER A RESOLVER HIT (block GC item 6). The nine-argument
// `RT_EXPORT_ONCE` onto `?Update@AnimationState@Graphics@@` is a QUALIFIED
// non-virtual member call on `Graphics::AnimationState`, whose body block GC also
// moved onto its member (`wave27_lane_gfxbig.cpp`). Its registered door is
// untouched and still serves every caller outside this file. The nine-argument
// mangled name that settled the argument mapping is kept in the comment: that is
// what the banner's "a stronger source than either" sentence was about.
// ---------------------------------------------------------------------------
void AnimationManager::Update(const System::Time& time, bool rootOnly) {
    int32_t groupIndex = 0;
    if (_instances._size <= 0) return;             // [RCX+8], Array::_size

    for (;;) {
        Instance* const g = &_instances._data[groupIndex];  // [RCX], reloaded each pass

        if (g->_group != nullptr && g->_states._size > 0) {
            // Control 5 hoists this bound. It is the EFFECTIVENESS control and
            // is EXPECTED TO REPORT NOT CAUGHT: nothing reachable here grows the
            // array, so the hoist is genuinely unobservable and a control set in
            // which every control fires proves only that the controls were
            // picked to fire (reference_probe_needs_an_effectiveness_control.md).
            for (int32_t i = 0; i < g->_states._size; ++i) {
                InstancedAnimationState* const state = &g->_states._data[i];   // stride 0x48

                // Rebuilt inside the loop, exactly as retail does (the callee
                // takes it by const reference and could keep the address).
                // `Transform()`: rotation (0,0,0) with w = 1.0f
                // ([0x18016169c] == 00 00 80 3f), translation zero.
                const Math::Transform base;

                // ?Update@AnimationState@Graphics@@QEAA_NAEBVTime@System@@AEBV?$Array@
                // UNode@GraphicsMesh@Graphics@@@Collection@@AEBVTransform@Math@@PEAV?$
                // InterpolatedState@VTransform@Math@@@2@PEAV78@AEAVVector3@8@_N6@Z
                // retail 0x1800fdb20 -- eight parameters plus `this`, in the order
                // that mangled name gives them. The 9th is retail's literal 0
                // (MOV byte [RSP+0x40],0), NOT the bool this function was given.
                // 5th arg `_nodes._data` (+0x38), 6th `_pose._data` (+0x28),
                // 7th `_rootOffset` (+0x18).
                state->_state.Update(
                        time, g->_nodes, base,
                        state->_nodes._data, state->_pose._data,
                        state->_rootOffset,
                        rootOnly, false);

                // Write the identity-rotation Transform carrying the Vector3 the
                // callee produced into *state->_pose._data. `_pose._data` is
                // RE-READ (`MOV RAX,[RDI+0x28]` at 0x1800fc33b) rather than reused.
                Math::Transform* const dst = state->_pose._data;
                dst->rotation.x = 0.0f; dst->rotation.y = 0.0f; dst->rotation.z = 0.0f;
                dst->rotation.w = 1.0f;
                // A trivially-copyable struct copy: bit-exact, as retail's three
                // integer moves are (see §5.3).
                dst->translation = state->_rootOffset;

                ++::g_animUpdStates;
            }
        }

        ++groupIndex;
        if (groupIndex >= _instances._size) break;     // the bound is RE-READ
    }
}

// ---------------------------------------------------------------------------
// ?Create@FrameCopyImageBuffer@Graphics@@  0x1800a3b70 -- SLOT 9, ENDGAME B.3.5
// block GC. The argument-to-offset map, the two-argument platform slot 3 call and
// the "RDX is `this`, NOT `_backBufferImage`" note are in the door's banner.
//
// Ten raw `base.*` POD writes became the real `Video::ImageBuffer` fields, which
// is what the inheritance chain is for; `self + 0x60` is `_data`.
// ---------------------------------------------------------------------------
bool FrameCopyImageBuffer::Create(ImageType imageType, Usage usage, Format format,
                                  uint32_t flags, int32_t width, int32_t height,
                                  FilterMode filterMode,
                                  AddressMode addressU, AddressMode addressV,
                                  AddressMode addressW, LevelArray* levels) {
    Attend("FrameCopyImageBuffer::Create", g_createCalls);   // B.3.9: from the retired door
    _dimension = imageType;           // +0x28  <- EDX      (0x1800a3b87)
    _kind      = usage;                    // +0x2c  <- R8D      (0x1800a3b8a)
    _format    = format;            // +0x30 <- R9D (0x1800a3b8e)
    _width     = width;                    // +0x20  <- arg5     (0x1800a3b92)
    _height    = height;                  // +0x24  <- arg6     (0x1800a3b99)
    _flags     = flags;                    // +0x34  <- arg4     (0x1800a3ba0)
    _filter    = filterMode;          // +0x44  <- arg7     (0x1800a3ba7)
    _addressU  = addressU;              // +0x38  <- arg8     (0x1800a3bae)
    _addressV  = addressV;              // +0x3c  <- arg9     (0x1800a3bb8)
    _addressW  = addressW;              // +0x40  <- arg10    (0x1800a3bc2)

    // `MOV RCX,[RCX+0x60]; MOV RAX,[RCX]; MOV RDX,<arg11>; CALL [RAX+0x18]` —
    // slot 3 of the platform companion, "create the texture and all its views"
    // (src/render/dx11/imagebufferdata.cpp:13), taking the level-descriptor array.
    // TWO arguments: retail sets RCX and RDX and nothing else.
    // `ImageBufferData::Create(levels)`, by name.
    const bool ok = _data->Create(levels);             // +0x60
    if (!ok) return false;                              // TEST AL,AL / JZ -> return AL

    // The second RenderBuffer, this time with `this` as the colour attachment.
    // Here retail calls `??2RenderBuffer@Video@@SAPEAX_K@Z` (0x18001f2e0) as a
    // REAL CALL rather than inlining it — `MOV ECX,0x30; CALL 0x18001f2e0` at
    // 0x1800a3be2. Same five instructions as the inlined form in the ctor, and
    // MemAlloc(0x30, 8) is that body (see the MemAlloc note above), so this is
    // spelled the same way for one arena and one reader.
    Video::RenderBuffer* const rb = static_cast<Video::RenderBuffer*>(
        System::MemoryAllocator::MemAlloc(0x30, 8));
    if (rb != nullptr) {
        RenderBufferZeroHeader(rb);
        RenderBufferPlatformConstruct(rb);
    }
    _destTarget = rb;                                   // +0x80

    // `XOR R8D,R8D; MOV RDX,RDI; MOV RCX,RBX` — colour is THIS, depth is null.
    // Note RDX is `this`, NOT `this->_backBufferImage`: that is the difference
    // between the ctor's RenderBuffer and this one, and it is the whole point
    // of the class (copy FROM the back-buffer alias INTO our own texture).
    RenderBufferCreate(rb, this, nullptr);

    return true;                                        // MOV AL,0x1
}

// ---------------------------------------------------------------------------
// ?Generate@FrameCopyImageBuffer@Graphics@@  0x1800a3b20 -- slot 10, block GC.
// `UEBA` is CONST and the body still writes `_generation` through it: the
// field is `mutable` (GeneratedImageBuffer.h, B.4 const_slot). The
// four-argument platform slot-3 call (`MOV R9B,0x1`) is unchanged.
// ---------------------------------------------------------------------------
FrameCopyImageBuffer::ExtAfterCopy FrameCopyImageBuffer::_extAfterCopy = nullptr;

void FrameCopyImageBuffer::Generate(void* context, int32_t generation) const {
    Attend("FrameCopyImageBuffer::Generate", g_generateCalls);   // B.3.9: from the retired door
    if (_generation == generation) return;              // CMP [RCX+0x68],R8D / JZ

    Attend("FrameCopyImageBuffer::Generate BLIT", g_generateCopies);

    // rax = _destTarget (+0x80); rcx = rax->platformData (+0x28); r8 = _sourceTarget.
    // `RenderBufferData::Resolve` (slot 3). LEFT: `RenderBuffer::platformData`
    // is `void*` (Device.h), viewed once as the companion it is.
    _destTarget->platformData   // faithful: unguarded
        ->Resolve(context, _sourceTarget, true);

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/BETTERWATER.md section 8): the scene depth copy
    // for ext.better_water's contact foam, beside the colour copy (FrameCopyImageBuffer.h).
    if (_extAfterCopy != nullptr) _extAfterCopy(context);

    _generation = generation;  // [RBX+0x68]: the const (UEBA) slot writes the mutable latch
}

}  // namespace Graphics
