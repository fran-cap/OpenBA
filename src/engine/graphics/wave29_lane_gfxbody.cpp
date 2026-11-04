// ============================================================================
// src/engine/graphics/wave29_lane_gfxbody.cpp — WAVE 29 LANE GFXBODY
// ============================================================================
//
// Graphics' remaining BODY rows. FOUR of the namespace's eight are ported here;
// the other four are declined on SIZE and the declination is stated as a size
// judgement in docs/history/runtime_port/WAVE29_LANE_GFXBODY.md §5, not dressed as a
// blocker.
//
// | symbol                                       | retail      | ord  | bytes |
// |----------------------------------------------|-------------|------|-------|
// | ?AddGlobalDrawItems@Scene@Graphics@@          | 0x180102330 | 1558 |   122 |
// | ?AddLight@Renderer@Graphics@@                 | 0x1801051d0 | 1567 |   104 |
// | ?EndDrawGroup@Renderer@Graphics@@             | 0x180104f50 | 2309 |   108 |
// | ?GetRootPoseAtTime@Animation@Graphics@@       | 0x1800faaa0 | 2880 |   495 |
//
// All four were already in the census `called` column before this lane: the
// first three through `RT_EXPORT_ONCE` doors in wave28_lane_partsys.cpp
// (lines 1049/1063/1070), the fourth through two doors in
// wave28_lane_gfxbody.cpp (lines 740/770). Lane PARTSYS reached three of them
// and did not port them; this lane closes them.
//
// ---------------------------------------------------------------------------
// PROVENANCE — objdump and pefile against the shipped DLL, not Ghidra
// ---------------------------------------------------------------------------
// Every address, ordinal, extent, constant and operand below was read with
//
//     objdump -d --start-address=… --stop-address=… -M intel
//             "C:/GOG Games/Banished/Runtime-x64.dll"
//     pefile against the same file for the export directory and .pdata
//
// Ghidra's decompiled C was not used at all here. It drops dereferences, prints
// MOVSXD as a zero extension and drops whole arguments at a mistyped vtable
// call (reference_ghidra_c_output_vs_disassembly.md), and GhidraMCP's
// get_function_metrics silently ignores `address=`
// (reference_ghidra_mcp_address_param_silently_ignored.md).
//
// ---------------------------------------------------------------------------
// EXTENT — DERIVED, and the walker that derived it was WRONG BY CONSTRUCTION
// ---------------------------------------------------------------------------
// Kickoff warning 16: a set difference over the wrong extent gives a real
// answer to a different question, and .pdata understated one extent by 24x.
//
// This lane's FIRST extent walker walked FORWARD and required the successor
// .pdata row to begin EXACTLY at the predecessor's end. That is a DEFLATING
// blind spot — alignment padding between fragments terminates the walk early
// and the body reports SHORTER than it is. Lane SLOTS' measurement of
// ?Draw@EdgeMeshInstance@ at 70.3x its first .pdata entry prompted the rewrite.
//
// The walker used for the figures above is PARENT-INDEXED: it builds
// {primary BeginAddress -> [chained rows]} over the whole .pdata section from
// the chained UNWIND_INFO tail and closes it transitively, never comparing
// addresses at all. Before it was believed on any subject here it reproduced
// three extents published independently by other lanes:
//
//     ?Update@ParticleSystemInstance@  157 -> 5242  (5 frags)  PARTSYS, 33.4x
//     ?Draw@EdgeMeshInstance@           25 -> 1757  (7 frags)  SLOTS,   70.3x
//     ?DrawShadowMap@SunLight@          50 ->  900  (4 frags)  SLOTS,   18.0x
//
// ALL THREE REPRODUCED EXACTLY. On this lane's own eight subjects the old and
// new walkers agree, so nothing measured earlier was wrong — but it was sound
// by luck and is now sound by construction, and that distinction is recorded
// rather than quietly repaired.
//
// Of the four ported here, THREE are single .pdata fragments and
// `GetRootPoseAtTime` is THREE chained fragments spanning 495 bytes from a
// 37-byte first entry — a 13.4x understatement had it been read off one row.
//
// ---------------------------------------------------------------------------
// THE WRITTEN-OPERAND SET OF EACH BODY — the mandated method (kickoff §2.2)
// ---------------------------------------------------------------------------
// `Camera::operator=` writes +0x60 as a single BYTE where the copy ctor writes
// a DWORD, and `MeshInstance::AddDrawItems` touches 19 fields of `this` where
// the EdgeMeshInstance override touches ONE. Neither is visible in decompiled
// C and both survive every semantic check. So each body below was reduced to
// the SET of byte offsets it writes, per base register, over the derived
// extent, and each port is checked against that set and nothing else.
//
// Measured (stack-frame register homes elided; they are not object state):
//
//   Scene::AddGlobalDrawItems   writes NOTHING AT ALL outside its own frame.
//                               3 memory-destination instructions, all
//                               register-save homes. It is `AEBA` — private
//                               CONST — and the operand set proves the const.
//   Renderer::AddLight          writes NOTHING through `this` directly; both
//                               mutations go through the two SetSize helpers.
//                               Its frame temporaries are rsp+0x20..0x2b and
//                               rsp+0x40..0x43 — SEE THE 12-of-16 NOTE BELOW.
//   Renderer::EndDrawGroup      writes EXACTLY rdi+0x34..0x37 — four bytes,
//                               `Renderer::currentDrawGroup`, and nothing else
//                               on `this`. The three sorts mutate the item
//                               arrays through the callee, not through `this`.
//   Animation::GetRootPoseAtTime writes EXACTLY rdi+0x00..0x1b — 0x1c bytes,
//                               the WHOLE Math::Transform return slot, and
//                               nothing else. That independently confirms
//                               sizeof(Math::Transform) == 0x1c and that the
//                               body fully initialises its hidden return.
//
// The reader that produced those sets parses objdump's already-decoded Intel
// operand text rather than enumerating opcode byte patterns. Wave 27's reader
// enumerated patterns, missed the no-displacement `MOV [RCX],EAX` form (89 01),
// and reported +0x00..+0x03 UNWRITTEN by BOTH Camera bodies — consistently and
// wrongly. Controls run before any subject: it now FINDS that exact form at
// 0x180017a72 and 0x180017c42, and it reproduces Wave 28's published Camera set
// difference exactly ({0x61,0x62,0x63}).
//
// ---------------------------------------------------------------------------
// A BY-VALUE RETURN IS A HIDDEN RDX RETURN SLOT
// ---------------------------------------------------------------------------
// `GetRootPoseAtTime` returns `Math::Transform` BY VALUE. An fn_* typedef that
// omits the hidden return slot is a wild write that still returns the right
// answer (reference_hidden_retslot_wild_write.md). Read out of the prologue and
// not assumed: RCX=this, RDX=&ret, R8=&arg1, R9=&arg2, and the epilogue does
// `MOV RAX,RDI` (0x1800fac25) where RDI was RDX — it RETURNS the slot.
// Lane PARTSYS already spells its neighbour correctly
// (wave28_lane_partsys.cpp:1042, `void* (*)(const void*, void*, void*)`), and
// this file matches that shape.
//
// ---------------------------------------------------------------------------
// CALLEES — classified against the export directory BEFORE anything was written
// ---------------------------------------------------------------------------
//   0x180023830  Array<uint32>::SetSize(n,&fill)   NOT EXPORTED, but PORTED as
//                RtColl_ArraySetSizeFill_180023830 (array.cpp J3_ENTRY3) and
//                registered, so registry-first ResolveAddr reaches OUR body.
//   0x180063a80  Array<0x10>::SetSize(n,&fill)     NOT EXPORTED. THIS LANE
//                PROVED IT IS THE SAME TEMPLATE INSTANTIATION as the known
//                kSpec_Fill16 site 0x18004f790 — 79 of 79 instructions
//                IDENTICAL under position-independent normalisation, with a
//                positive control (a function against itself, 79/79) and a
//                negative control (an unrelated function, 0/79). It is now
//                registered as such in array.cpp, which converts what would
//                have been an address-door residual into a ported one.
//   0x180062d60  the draw-item sort                NOT EXPORTED, NOT PORTED.
//                ~0x2306 bytes of introsort with a 0x400-byte recursion stack.
//                Reached by address. RESIDUAL — see §8 of the lane doc; this is
//                the same class as Wave 27 lane GFXBIG's R1 and Wave 28's R1.
//   vtable +0x18 AddDrawItems(const Scene&, Renderer&) — slot 3, a virtual
//                call, no door of any kind.
//   0x1801fe728  ?_secondsPerTick@Timer@System@@0NA — a NAMED DATA export,
//                ordinal 4797, reached here by NAME. Wave 28 residual R5 said
//                a name door was available and nobody had taken it; NINE files
//                already had. See the SecondsPerTick() comment below.
//
// ---------------------------------------------------------------------------
// CONSTANTS — LIFTED, never retyped
// ---------------------------------------------------------------------------
//   0x18016169c = 00 00 80 3f = 1.0f, read out of the file with pefile. Not an
//   export; lifted as bytes. (reference_crt_coefficients_must_be_lifted_not_retyped.md)
//
//   0x1801fe728 is NOT a liftable constant. Its RVA lies beyond .data's
//   SizeOfRawData — the uninitialised tail — so there are no bytes in the file
//   and a port that baked in "the constant" would have baked in ZERO. It is
//   read at runtime, through the name door above.
//
// Kill switch: RT_GFXBODY29=0. Its own variable, deliberately NOT
// shared with RT_GFXBODY28 or RT_PARTSYS, so that with
// PARTSYS armed and this off, PARTSYS's SunLight::AddDrawItems runs and reaches
// the DLL for exactly these three callees — which separates "the caller is
// wrong" from "one of its callees is". Sharing a variable destroys that A/B.
// ============================================================================

#include <cstdint>
#include <cstdio>   // unqualified printf: log_strip.h #defines it away
#include <cstring>

#include "graphics/Animation.h"   // Graphics::Animation -- ENDGAME B.3.5 block GC
#include "graphics/Camera.h"   // Renderer, RendererDrawGroup, RendererDrawable
#include "graphics/Scene.h"    // Scene, _globals @ +0x60
#include "math/Math.h"         // Math::Transform (0x1c), GLOBAL scope
#include "system/Value.h"      // System::Time -- ENDGAME B.3.5 block GC
#include "system/Timer.h"      // ENDGAME B.3.9 SS1: Timer::_secondsPerTick

namespace rtgfxbody29 {
namespace {

// `Transform` is at GLOBAL scope in src/engine/math/Math.h -- the header is
// named for the Math NAMESPACE of the DLL, but declares its structs unwrapped.
// Checked rather than assumed: `grep -n namespace Math.h` finds only
// RtMathConst. No using-declaration is possible or needed.

// ---------------------------------------------------------------------------
// Attendance. Counted per BODY and, where a body has a branch whose absence an
// entry count cannot express, per BRANCH as well —
// `calls=N branch=0` and `calls=0` are DIFFERENT findings
// (reference_calls_zero_can_mean_always_took_the_uncovered_branch.md).
//
// THE SUMMARY BELOW IS WIRED INTO src/app/main.cpp. Wave 28 defect #7 is
// that every lane's *Summary() is written and nothing calls it, which makes a
// zero mean "never printed" rather than "never called" — and Wave 28 lane
// GFXBODY could report BeginDrawGroup at 1,600,000 from the Attend() milestone
// ladder while being UNABLE to report its own branch counter at all.
// ---------------------------------------------------------------------------
uint64_t g_addGlobal        = 0;   // Scene::AddGlobalDrawItems entries
uint64_t g_addGlobalVisited = 0;   // global-list nodes walked
uint64_t g_addGlobalEmitted = 0;   // nodes that passed BOTH tests and dispatched
uint64_t g_addLight         = 0;   // Renderer::AddLight entries
uint64_t g_endDrawGroup     = 0;   // Renderer::EndDrawGroup entries
uint64_t g_endDrawSorted    = 0;   // per-pass lists that actually reached the sort
uint64_t g_rootPoseLerped   = 0;   // took the TWO-KEY interpolation arm
uint64_t g_rootPoseSingle   = 0;   // took the SINGLE-KEY arm

// The shipped float constant, as the bit pattern read out of Runtime-x64.dll.
inline float FromBits(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}
const float kOne = FromBits(0x3f800000u);   // 0x18016169c

// ---------------------------------------------------------------------------
// System::Timer::_secondsPerTick, through the NAME door.
//
// This is the ORDINARY house pattern here, not a new door. Saying so because an
// earlier draft of this comment claimed otherwise.
//
// WAVE 28 RESIDUAL R5 WAS STALE ON ARRIVAL. It said 0x1801fe728 is the named
// export `?_secondsPerTick@Timer@System@@0NA` (ord 4797) — TRUE, re-confirmed
// against the export directory — and that "a name door is available and nobody
// has taken it" — FALSE WHEN WRITTEN. Measured on wave29/base before this file
// existed: NINE files already take that exact name door via RT_DATA_SLOT
// (audio_scene_update.cpp:302, input_device_update.cpp:170,
// input_device_update_probe.cpp:628, timer.cpp:166, userinterface.cpp:270,
// userinterface_close.cpp:174, userinterface_frame.cpp:175,
// userinterface_hover.cpp:338, userinterface_standalone.cpp:141) plus
// userinterface_showdialog.cpp:192 via ResolveExport; the symbol is already
// MINTED (rt_data_mint_tables.inc:2291) and already carried CUTOVER_REQUIRED
// (cutover_manifest.inc:75).
//
// The census confirmed it the only way that cannot be argued with: adding this
// door moved the `called` column by ZERO, because the row was already there.
// What is genuinely open is ONE address door, engine_funcs.cpp:1546, which this
// file does not touch. See WAVE29_LANE_GFXBODY.md §2.4.1.
//
// `0NA` means PRIVATE, not unexported — this DLL exports its private members
// (reference_aeaa_is_private_not_unexported.md).
//
// RT_DATA_SLOT never caches a null, so a phantom name would fail loudly rather
// than silently resolving to zero and baking a 0.0 scale into every root pose
// (reference_resolveexport_phantom_names.md).
// ---------------------------------------------------------------------------
inline double SecondsPerTick() {
    // ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, read fresh.
    return ::System::Timer::_secondsPerTick;
}

// ---------------------------------------------------------------------------
// Callees.
// ---------------------------------------------------------------------------

// ENDGAME B.3.2 step 2 (callers) — the `ArraySetSizeFill4` (0x180023830) and
// `ArraySetSizeFill16` (0x180063a80) ResolveAddr forwarders that used to sit
// here are GONE. Their one caller, `RtGraphics_Renderer_AddLight` below, now
// calls `.SetSize(n, fill)` directly on `RendererSceneEntry::drawableIds` and
// `Renderer::drawables` — both real `Collection::Array<T>` members
// (src/engine/graphics/Camera.h) whose element types (`int32_t`,
// `RendererDrawable`) make kSpec_Fill4/kSpec_Fill16 trait-equivalent. Both
// rows stay registered in array.cpp: `code/` still reaches them by address.

// The per-pass draw-item sort — 0x180062d60, UNEXPORTED; ported as
// rtgfxbig::RtGraphics_Renderer_DrawItemSort (wave27_lane_gfxbig.cpp, declared
// in Scene.h), called by name (B3R). std::sort(first, last) over stride-0x38
// elements; `last` is INCLUSIVE: the callee computes `first - 0x38 + count*0x38`.
inline void DrawItemSort(Graphics::RendererDrawItem* data, int count, void* scratch) {
    rtgfxbig::RtGraphics_Renderer_DrawItemSort(data, count, scratch);
}

}  // namespace

// ============================================================================
// ?AddGlobalDrawItems@Scene@Graphics@@AEBAXAEAVRenderer@2@@Z   0x180102330
//
//   private: void Scene::AddGlobalDrawItems(Renderer&) const
//
// The "global" half of the scene — objects that skip the grid and are visited
// every frame regardless of the cull. `Scene.h` already documents the pairing:
// a hash grid plus "a separate doubly linked list of global objects that skip
// the grid and are always visited". This body is the visitor.
//
// WRITTEN-OPERAND SET: EMPTY outside the frame. It is const and the operand
// set PROVES the const rather than trusting the `AEBA` in the name.
//
// Retail, 0x180102330..0x1801023aa:
//
//   r8   = (int64)renderer->currentDrawGroup       ; MOVSXD, +0x34
//   rax  = renderer->drawGroups._data              ; +0x10
//   rbx  = scene->_globals._head                   ; +0x60
//   r8  *= 0x1f0                                   ; sizeof(RendererDrawGroup)
//   esi  = *(int*)(rax + r8 + 0x1bc)               ; group.passMask
//   for (node = rbx; node; node = node->next)      ; next is at node+0x10
//       obj = node->element                        ; element is at node+0x00
//       if ((obj->[0x30] & passMask) == 0) continue          ; TEST/JE
//       if (obj->[0x28] > renderer->[0x48]) continue          ; CMP/JG
//       (*(vtbl[3]))(obj, scene, renderer)                    ; CALL [rax+0x18]
//
// THE 0x1f0 STRIDE IS CONFIRMED THREE INDEPENDENT WAYS IN THIS LANE, which is
// why it is stated as measured rather than inherited: here as a literal
// `IMUL R8,R8,0x1f0`; in `EndDrawGroup` below as `IMUL RCX,RCX,0x1f` followed
// by `SHL RCX,4` (0x1f * 16 = 0x1f0); and by Wave 28 residual R1's
// independently-derived `Array<RendererDrawGroup>::SetSize` stride. The
// existing static_assert in Camera.h agrees.
//
// The `+0x1bc` read is `RendererDrawGroup::passMask`, ALREADY NAMED in
// Camera.h from AddDrawItem's side. This body is a second, independent reader
// of that field in the same role — and that matters, because N readers
// agreeing is weak evidence when they test a SIGN
// (reference_a_writer_beats_readers_agreeing_on_a_sign.md) but this is a
// BITWISE AND against a per-object mask, not a sign test.
//
// TWO FIELDS OF THE OBJECT ARE READ AND THIS LANE DOES NOT NAME EITHER.
// `obj->[0x30]` is a mask AND'd against the group's passMask, and `obj->[0x28]`
// is compared `>` against `Renderer::field_48`. A `>` against a threshold is
// exactly what an LOD level, a layer index and a priority share, and this lane
// found NO WRITER for either field. Per
// reference_only_a_writer_names_a_field.md they are left UNNAMED here rather
// than guessed, and they are read through a byte offset for that reason.
// ============================================================================

// ============================================================================
// ?AddLight@Renderer@Graphics@@QEAAXPEBVLight@2@@Z              0x1801051d0
//
//   public: void Renderer::AddLight(const Light*)
//
// Appends the light to the renderer's drawable array AND records its index in
// the CURRENT SCENE ENTRY's id list, in that order — and the order is
// load-bearing, because the index recorded is the array size BEFORE the append.
//
// WRITTEN-OPERAND SET: nothing through `this`. Both mutations go through the
// two SetSize helpers. The frame temporaries are rsp+0x20..0x2b and
// rsp+0x40..0x43.
//
// *** THE 12-OF-16 NOTE, AND IT IS THE WRITE-WIDTH TRAP IN THIS BODY. ***
// The drawable record is SIXTEEN bytes (`RendererDrawable`), and retail writes
// only TWELVE of them: `MOV [RSP+0x20],RBX` (the Light*) and
// `MOV [RSP+0x28],EAX` (the int). Offsets 0x2c..0x2f — `RendererDrawable::
// _pad0c` — are NEVER WRITTEN, and the operand reader reports exactly
// `0x20-0x2b` with a hole at 0x2c. The SetSize helper then copies all 16 bytes
// regardless, so retail stores UNINITIALISED STACK into the array on every
// append. Camera.h already carries that note for the sibling draw-item array;
// this body is an independent instance of it, found mechanically rather than
// by reading the header first.
//
// SO THIS PORT MUST NOT ZERO-INITIALISE THE RECORD. `RendererDrawable rec;`
// with two assignments is FAITHFUL; `RendererDrawable rec{};` or `= {}` is
// NOT, and would make our appended element differ from retail's in four bytes
// that an oracle comparing the array would then flag — or, worse, would not
// (reference_oracle_must_not_compare_unwritten_bytes.md).
//
// Retail, 0x1801051d0..0x180105238:
//
//   r8   = (int64)renderer->currentScene * 0x110   ; +0x30, MOVSXD then IMUL
//   eax  = renderer->drawables._size               ; +0x28  <-- BEFORE the append
//   rcx  = renderer->scenes._data + 0x100 + r8     ; sceneEntry.drawableIds
//   [rsp+0x40] = eax
//   Array<uint32>::SetSize(rcx, rcx->_size + 1, &[rsp+0x40])    ; 0x180023830
//   edx  = renderer->drawables._size + 1           ; RE-READ, +0x28
//   rcx  = &renderer->drawables                    ; +0x20
//   [rsp+0x20] = light ; [rsp+0x28] = renderer->field_48        ; +0x38?? NO:
//                                                  ; `MOV EAX,[RDI+0x38]`
//   Array<16>::SetSize(rcx, edx, &[rsp+0x20])                   ; 0x180063a80
//
// *** A CORRECTION THIS LANE MADE TO ITS OWN FIRST READING. *** The int stored
// beside the Light* comes from `[RDI+0x38]` (0x180105211, `MOV EAX,[RDI+0x38]`)
// — `Renderer::field_38` — NOT from `[RDI+0x48]`. The first pass through this
// body recorded +0x48 because +0x48 IS read two instructions away in the
// sibling body above, and the two were conflated. Re-read off the listing.
// Camera.h's comment on field_38 says src/diag/scene_watch.h:73 reads it as
// "draw groups used this frame" and that lane F ported nothing that touches it,
// so that reading was "neither confirmed nor contradicted". THIS BODY IS A
// READER OF IT, and it stores it per-light — which is consistent with a
// per-frame or per-group tag, and is NOT enough to settle the name. Left as
// `field_38`.
// ============================================================================

// ============================================================================
// ?EndDrawGroup@Renderer@Graphics@@QEAAXH@Z                     0x180104f50
//
//   public: void Renderer::EndDrawGroup(int)
//
// Closes the CURRENT draw group by sorting each of its three per-pass item
// lists, then makes the argument the current group.
//
// WRITTEN-OPERAND SET: EXACTLY rdi+0x34..0x37. Four bytes. One field.
// Everything else this body changes, it changes through the sort callee.
// That is the whole value of the operand-set discipline on a body this small:
// it converts "it probably only sets the index" into a measurement.
//
// Retail, 0x180104f50..0x180104fbc:
//
//   for (i = 0; i < 3; ++i) {
//       idx  = (int64)this->currentDrawGroup        ; RE-READ EVERY ITERATION
//       off  = (idx*0x1f + 0x1c + i) << 4
//       p    = this->drawGroups._data + off         ; == &group.items[i]
//       n    = p->_size
//       if (n > 0 && n > 1) sort(p->_data, n, &scratch)
//   }
//   this->currentDrawGroup = arg
//
// TWO DETAILS THAT A "TIDY" PORT LOSES:
//
//  1. `currentDrawGroup` is RE-READ INSIDE THE LOOP (`MOVSXD RCX,[RDI+0x34]` at
//     0x180104f70) and only assigned AFTER it. Hoisting the read is equivalent
//     ONLY because the sort cannot reach back into the Renderer — which is an
//     assumption about a 0x2306-byte unexported callee this lane has NOT
//     verified. So it is re-read here, matching the listing, and the assumption
//     is not taken.
//  2. Retail tests the count TWICE: `TEST EDX,EDX / JLE` then `CMP EDX,1 / JLE`.
//     The first test is redundant. It is reproduced rather than folded, because
//     folding it is a judgement that they are equivalent and the cost of
//     keeping it is nothing.
//
// The `0x1c + i` then `<< 4` arithmetic lands on `group + 0x1c0 + i*0x10`,
// i.e. `RendererDrawGroup::items[i]`, and `items` is at +0x1c0 with stride 0x10
// in Camera.h. The element stride the SORT uses is 0x38 — `IMUL RAX,RAX,0x38`
// and the magic multiplier 0x4924924924924925 in the callee — which is
// `sizeof(RendererDrawItem)`, also already 0x38 in Camera.h. Neither number was
// taken from the header; both were read out of the two bodies and then found to
// agree with it.
//
// The third argument is a pointer to EIGHT BYTES OF THE CALLER'S OWN HOME SLOT
// (`LEA R8,[RSP+0x38]`, which is the RDX home of this frame) used as scratch.
// It is NOT an output anyone reads. A `uint64_t` local serves.
// ============================================================================

// ============================================================================
// ?GetRootPoseAtTime@Animation@Graphics@@QEBA?AVTransform@Math@@AEBVTime@System@@0@Z
//                                                               0x1800faaa0
//
//   public: Math::Transform Animation::GetRootPoseAtTime(
//               System::Time const&, System::Time const&) const
//
// Samples the ROOT node's translation from the clip's key array at the first
// time, and scales it by the second time. The returned rotation is the IDENTITY
// quaternion, always.
//
// WRITTEN-OPERAND SET: EXACTLY rdi+0x00..0x1b — the whole 0x1c-byte return
// slot and nothing else. So the hidden return is fully initialised and there is
// no window an oracle must avoid comparing.
//
// HIDDEN RETURN SLOT: RCX=this, RDX=&ret, R8=&t0, R9=&t1, and `MOV RAX,RDI`
// (0x1800fac25) returns the slot. See the banner.
//
// --- WHAT THE FIELDS ARE, AND WHERE THIS LANE REFUSES TO NAME ONE ----------
// `this` is the `Graphics::Animation` clip record. Wave 27 lane GFXBIG built
// `AnimationClipPrefix` (src/engine/graphics/AnimationState.h:112) entirely
// from AnimationState::Update's READS of it, and Wave 28 lane GFXBODY confirmed
// six of its offsets from ApplyEvents, a MEMBER of the same class. This body is
// a third reader and it fills THREE of that struct's `_unpinned_` holes:
//
//   +0x40  loopFrame   already named. Read here as `if (loopFrame == 0) ebx = 0`
//                      then `ebx = ((loopFrame - frameFirst)/(frameLast -
//                      frameFirst)) * [0x50]` — the key index of the loop start.
//                      The SAME three-term expression ApplyEvents uses. Third
//                      independent agreement.
//   +0x44  _unpinned   A FLOAT, and this body multiplies SECONDS by it to get a
//                      key position, and multiplies the second Time by it too.
//                      Structurally that is KEYS PER SECOND. Stated as a
//                      structural reading, not a name: this lane has no WRITER.
//   +0x4c  _unpinned   An INT multiplied by a key index BEFORE a *0x1c stride,
//                      i.e. ELEMENTS PER KEY. That is a stride multiplier, not
//                      a sign test, so it is a strong reading — but again, no
//                      writer, so it is not entered into the header as a name.
//   +0x50  frameRate   *** A NAME TENSION THIS LANE REPORTS RATHER THAN
//                      SILENTLY RESOLVES. *** AnimationState.h calls +0x50
//                      `frameRate`. In THIS body it is used as the EXCLUSIVE
//                      UPPER BOUND on a key index (`CMP ESI,R8D / JL`) and as
//                      the modulus base for the loop wrap — i.e. as a key
//                      COUNT. Both readings scale frame->index identically and
//                      both readers are consistent with either name. NO WRITER
//                      HAS BEEN FOUND, so the field stays named as the header
//                      already has it and the tension is recorded here.
//   +0x3c  looping     already named; read here exactly as the header says —
//                      zero CLAMPS to the last key, non-zero WRAPS.
//   +0x60  (beyond the 0x60 prefix) the key array base. Elements are 0x1c bytes
//                      — a Math::Transform — and the body reads +0x10/+0x14/
//                      +0x18 of element `keyIndex * [0x4c]`, i.e. the
//                      TRANSLATION of node 0. That is why it is the ROOT pose.
//
// --- THE MOVED-TERM HAZARD, WHICH IS LIVE IN THIS BODY ----------------------
// A moved term in a float sum is algebraically identical and numerically
// different (reference_moved_term_hides_where_terms_cancel.md). Two places:
//
//  1. Both time conversions NARROW TO FLOAT BEFORE the multiply:
//     `CVTSI2SD` (int64 ticks to double), `MULSD` by _secondsPerTick (a
//     DOUBLE), then `CVTPD2PS` to float, and ONLY THEN `MULSS` by the +0x44
//     rate. Doing the whole chain in double and narrowing once is a DIFFERENT
//     number. Written below exactly in that order, with explicit `(float)`.
//  2. The interpolation is `a + (b - a) * t`, in that association — `SUBSS`,
//     `MULSS`, `ADDSS`, per component (0x1800fabda..0x1800fabfa). NOT
//     `a*(1-t) + b*t`, which is the same value in real arithmetic and not in
//     float. Written as retail associates it.
//
// --- THE FRACTION IS TAKEN FROM THE *UNCLAMPED* INDEX -----------------------
// A one-line error that produces a working animation with wrong timing. `esi`
// is `(int)keyPos` computed BEFORE any clamp or wrap; the clamp/wrap write
// `r9d`/`r11d` and LEAVE `esi` alone; and the fraction at 0x1800fab83 is
// `keyPos - (float)esi` — the RAW truncation, not the wrapped index. Retail
// reloads `esi` for that subtraction after the wrap has already happened.
//
// --- THE SINGLE-KEY TEST, INCLUDING ITS NaN ARM -----------------------------
//   `CMP R9D,R11D / JE`            -> same key twice: sample once.
//   `UCOMISS XMM3,XMM4 / JP / JE`  -> JP jumps to the LERP arm when UNORDERED,
//                                     JE to the single arm when EQUAL.
// So: single iff the two indices coincide, OR the fraction is exactly 0.0f.
// A NaN fraction takes the LERP arm. `frac == 0.0f` is false for NaN in C++,
// so the ordinary spelling below reproduces both arms without a special case —
// stated because it looks like an omission.
// ============================================================================

}  // namespace rtgfxbody29

// ############################################################################
// ENDGAME B.3 step 5 (block F) — THE TWO `Renderer` BODIES, MOVED ONTO MEMBERS
// ############################################################################
//
// `Graphics::Renderer` is a real C++ class as of block F
// (`src/engine/graphics/Camera.h`), so `AddLight` and `EndDrawGroup` move onto
// it and their doors above become one-line QUALIFIED delegations. The attendance
// counters move WITH the bodies, so a C++ caller is counted exactly as a
// by-name caller is; `RtGraphicsWave29GfxbodySummary()` is unchanged.
//
// `Scene::AddGlobalDrawItems` and `Animation::GetRootPoseAtTime` do NOT move
// here: the first lands with `Graphics::Scene` in block F's next commit, and the
// second belongs to `Graphics::Animation`, which is BLOCK H1's class — its
// header already carries the declaration and says the door in this file is "not
// ours". Both are listed in block F's report.
//
// TWO FIELD NAMES THIS CONVERSION SETTLES, both recorded as open by the banner at
// the top of this file:
//   * `Renderer::field_38`, which `AddLight` stores into every drawable entry,
//     is DRAW GROUPS USED THIS FRAME. This lane wrote "consistent with a
//     per-frame or per-group tag, and is NOT enough to settle the name"; block F
//     settled it from the WRITER — `BeginDrawGroup` (0x180104fd0) reads +0x38 as
//     the index of the group it opens, compares it against `_drawGroups._size`
//     and increments it (`INC dword ptr [RDI+0x38]` at 0x18010509e). That also
//     closes B35_RULES §13 item 10 and confirms `src/diag/scene_watch.h:73`.
//   * `Renderer::field_48`, which `AddGlobalDrawItems` compares an object's
//     +0x28 against, is the DETAIL ID: `?GetDetailId@Renderer@Graphics@@QEBAHXZ`
//     (0x180018070) is `MOV EAX,[RCX+0x48]; RET`. The object-side +0x28 is
//     `Geometry::_detailId`, which block E already named.
// ############################################################################

namespace Graphics {

// ---------------------------------------------------------------------------
// ?AddGlobalDrawItems@Scene@Graphics@@AEBAXAEAVRenderer@2@@Z  retail 0x180102330
//
// The "global" half of the scene — objects that skip the grid and are visited
// every frame regardless of the cull. `AEBA`: a PRIVATE const member that this
// DLL exports anyway, declared public on `Graphics::Scene` for the reason
// Scene.h records (its registered door is a free function).
//
// WRITTEN-OPERAND SET: EMPTY outside the frame. The operand set PROVES the const
// rather than trusting the `AEBA` in the name.
//
// THREE RAW BYTE READS THIS CONVERSION RETIRES. This lane wrote "TWO FIELDS OF
// THE OBJECT ARE READ AND THIS LANE DOES NOT NAME EITHER", and left `obj+0x30`,
// `obj+0x28` and `renderer+0x48` as byte reads per
// reference_only_a_writer_names_a_field.md. All three are named now, from
// WRITERS and from accessor exports rather than from more readers:
//   obj + 0x30      `Geometry::_materialTypeFlags` — block E's class; retail's
//                   `?GetMaterialTypeFlags@Geometry@` (0x1800157f0) returns it,
//                   and `?GetMaterialTypeFlags@Renderer@` (0x180017ff0) returns
//                   the draw group's `_materialTypeFlags` this ANDs against.
//   obj + 0x28      `Geometry::_detailId` — `?GetDetailId@Geometry@`
//                   (0x180015890), and the Geometry constructor writes 2 there.
//   renderer + 0x48 `Renderer::_detailId` — `?GetDetailId@Renderer@`
//                   (0x180018070) is `MOV EAX,[RCX+0x48]; RET`. Block F read it
//                   this pass; see Camera.h correction 2.
// So the `>` test is a DETAIL-LEVEL threshold on both sides, which is what this
// lane suspected and could not name.
//
// The vtable slot 3 call is now an ORDINARY VIRTUAL CALL on a
// `Graphics::Geometry*` (block E declared the slot; `cull.cpp`'s sibling loop
// converted the same way).
//
// `Renderer::_detailId` is RE-READ INSIDE THE LOOP (`[rdi+0x48]` at
// 0x180102378), once per surviving node, because the virtual call could in
// principle mutate it. Hoisting it would be a different program.
// ---------------------------------------------------------------------------
void Scene::AddGlobalDrawItems(Renderer& rendererRef) const {
    ++rtgfxbody29::g_addGlobal;

    const Renderer* renderer = &rendererRef;
    const uint32_t passMask =
        renderer->_drawGroups[renderer->_currentDrawGroup]._materialTypeFlags;

    for (auto it = _globals.GetFirst(); it; ++it) {
        ++rtgfxbody29::g_addGlobalVisited;
        Geometry* const obj = *it;

        if ((obj->_materialTypeFlags & passMask) != 0) {
            if (obj->_detailId <= renderer->_detailId) {       // re-read, per retail
                obj->AddDrawItems(*this, rendererRef);         // VIRTUAL, slot 3
                ++rtgfxbody29::g_addGlobalEmitted;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// ?AddLight@Renderer@Graphics@@QEAAXPEBVLight@2@@Z — retail 0x1801051d0.
//
// Appends the light to the renderer's drawable array AND records its index in
// the CURRENT SCENE GROUP's id list, in that order — and the order is
// load-bearing, because the index recorded is the array size BEFORE the append.
//
// *** THE 12-OF-16 NOTE, AND IT IS THE WRITE-WIDTH TRAP IN THIS BODY. *** The
// drawable record is SIXTEEN bytes (`RendererDrawable`) and retail writes only
// TWELVE of them: `MOV [RSP+0x20],RBX` (the Light*) and `MOV [RSP+0x28],EAX`
// (the int). `RendererDrawable::_pad0c` is NEVER WRITTEN, and the SetSize helper
// copies all sixteen bytes regardless, so retail stores UNINITIALISED STACK into
// the array on every append. SO THIS PORT MUST NOT ZERO-INITIALISE THE RECORD:
// `RendererDrawable rec;` with two assignments is FAITHFUL; `rec{}` is not
// (reference_oracle_must_not_compare_unwritten_bytes.md).
// ---------------------------------------------------------------------------
void Renderer::AddLight(const Light* light) {
    ++rtgfxbody29::g_addLight;

    // The index this light will occupy — read BEFORE the append.
    const int32_t lightIndex = _drawables.GetSize();

    // retail 0x180023830 (kSpec_Fill4) — trait-equivalent for int32_t.
    RendererSceneEntry& entry = _sceneGroups[_currentSceneGroup];
    entry._drawableIds.SetSize(entry._drawableIds.GetSize() + 1, lightIndex);

    RendererDrawable rec;                      // NOT value-initialised — banner
    rec._object = light;
    rec._value  = _drawGroupsUsed;             // retail: MOV EAX,[RDI+0x38]

    // retail 0x180063a80, kSpec_Fill16 — trait-equivalent: RendererDrawable is
    // a plain POD (Camera.h).
    _drawables.SetSize(_drawables.GetSize() + 1, rec);
}

// ---------------------------------------------------------------------------
// ?EndDrawGroup@Renderer@Graphics@@QEAAXH@Z — retail 0x180104f50.
//
// Closes the CURRENT draw group by sorting each of its three per-pass item
// lists, then makes the argument the current group. WRITTEN-OPERAND SET:
// EXACTLY rdi+0x34..0x37 — four bytes, `_currentDrawGroup`, and nothing else on
// `this`.
//
// TWO DETAILS THAT A "TIDY" PORT LOSES:
//  1. `_currentDrawGroup` is RE-READ INSIDE THE LOOP (`MOVSXD RCX,[RDI+0x34]` at
//     0x180104f70) and only assigned AFTER it. Hoisting the read is equivalent
//     ONLY because the sort cannot reach back into the Renderer — an assumption
//     about a 0x2306-byte unexported callee this lane has NOT verified. So it is
//     re-read here, matching the listing, and the assumption is not taken.
//  2. Retail tests the count TWICE: `TEST EDX,EDX / JLE` then `CMP EDX,1 / JLE`.
//     The first test is redundant. Reproduced rather than folded.
//
// The third sort argument is a pointer to EIGHT BYTES OF THE CALLER'S OWN HOME
// SLOT (`LEA R8,[RSP+0x38]`, this frame's RDX home) used as scratch. It is NOT an
// output anyone reads; a `uint64_t` local serves.
// ---------------------------------------------------------------------------
void Renderer::EndDrawGroup(int group) {
    ++rtgfxbody29::g_endDrawGroup;

    uint64_t scratch = 0;   // retail passes its own unread RDX home slot
    for (int64_t i = 0; i < 3; ++i) {
        const int64_t idx = _currentDrawGroup;               // re-read, per retail
        // retail's `((idx*0x1f)+0x1c+i)<<4` byte arithmetic lands on
        // `&_drawGroups[idx]._items[i]` — named directly.
        Collection::Array<RendererDrawItem>& list = _drawGroups[idx]._items[i];

        const int32_t n = list.GetSize();
        if (n > 0) {
            if (n > 1) {                                     // both tests, per retail
                RendererDrawItem* data = list.GetData();
                rtgfxbody29::DrawItemSort(data, n, &scratch);
                ++rtgfxbody29::g_endDrawSorted;
            }
        }
    }
    _currentDrawGroup = group;
}

// ---------------------------------------------------------------------------
// ?GetRootPoseAtTime@Animation@Graphics@@QEBA?AVTransform@Math@@AEBVTime@System@@0@Z
// retail 0x1800faaa0 -- ENDGAME B.3.5 block GC. The key-index arithmetic, the
// short-circuited loop fraction and the "single iff the two indices coincide OR
// the fraction is exactly 0.0f" arm selection are in the door's banner.
// ---------------------------------------------------------------------------
Math::Transform Animation::GetRootPoseAtTime(const System::Time& time,
                                             const System::Time& delta) const {
    Transform out;
    Transform* const ret = &out;
    const int64_t* const t0 = &time._ticks;
    const int64_t* const t1 = &delta._ticks;
    // B.4: `Graphics::Animation`'s own fields (Animation.h).
    const int32_t frameFirst = _startFrame;        // +0x30
    const int32_t frameLast  = _endFrame;          // +0x34
    const bool    looping    = _looping;           // +0x3c
    const int32_t loopFrame  = _loopFrame;         // +0x40
    const float   rate       = _frameRate;         // +0x44
    const int32_t perKey     = _boneCount;         // +0x4c  keys per frame
    const int32_t keyCount   = _frameCount;        // +0x50
    const Math::Transform* const keys = _keys._data;   // +0x60, stride 0x1c

    // The loop-start KEY INDEX. Retail short-circuits the whole fraction to 0
    // when loopFrame is zero rather than computing it.
    //
    // NOT RETAIL (UB, review F-121): the same unguarded arithmetic as
    // GetPoseAtTime (animationstate.cpp): a loop frame equal to the end frame
    // divides by `keyCount - loopKey` == 0, start == end converts a NaN or an
    // infinity, and a loop frame outside [start, end) indexes outside `_keys`.
    // A loop key outside [0, keyCount), or a clip with no keys, holds the last
    // key instead of wrapping. Every valid clip keeps retail's arithmetic.
    bool wraps = looping && keyCount > 0;
    int32_t loopKey = 0;
    if (loopFrame != 0) {
        const float num = static_cast<float>(loopFrame - frameFirst);
        const float den = static_cast<float>(frameLast - frameFirst);
        const float at = (num / den) * static_cast<float>(keyCount);
        if (at > -1.0f && at < static_cast<float>(keyCount))
            loopKey = static_cast<int32_t>(at);
        else
            wraps = false;
    }

    const double secondsPerTick = rtgfxbody29::SecondsPerTick();

    // NARROW, THEN SCALE — see the banner. cvtsi2sd / mulsd / cvtpd2ps / mulss.
    float keyPos = static_cast<float>(static_cast<double>(*t0) * secondsPerTick);
    keyPos *= rate;
    // NOT RETAIL (UB, review F-121): a negative or out-of-range time samples key 0.
    if (!(keyPos > -1.0f && keyPos < 2147483648.0f))
        keyPos = 0.0f;

    const int32_t rawKey = static_cast<int32_t>(keyPos);   // cvttss2si, truncates

    // Clamp or wrap the first key.
    int32_t k0 = rawKey;
    if (k0 >= keyCount) {
        if (!wraps) {
            k0 = keyCount - 1;
        } else {
            k0 = loopKey + (rawKey % (keyCount - loopKey));
        }
    }

    // ...and the second, which is k0+1 BEFORE wrapping, not k0's successor
    // after it. Retail computes `LEA R11D,[R9+1]` from the ALREADY-wrapped k0.
    int32_t k1 = k0 + 1;
    if (k1 >= keyCount) {
        if (!wraps) {
            k1 = keyCount - 1;
        } else {
            k1 = loopKey + (k1 % (keyCount - loopKey));
        }
    }

    // The fraction uses the RAW truncation, not the wrapped index.
    const float frac = keyPos - static_cast<float>(rawKey);

    float x, y, z;
    if (keyCount <= 0) {
        // NOT RETAIL (UB, review F-121): no key to read (retail reads
        // `keys - perKey`); the root offset is zero.
        x = 0.0f;
        y = 0.0f;
        z = 0.0f;
    } else if (k0 == k1 || frac == 0.0f) {
        // The frame's first key (the root), its translation (+0x10).
        const Math::Vector3& e = keys[static_cast<int64_t>(perKey * k0)].translation;
        x = e.x;
        y = e.y;
        z = e.z;
        ++rtgfxbody29::g_rootPoseSingle;
    } else {
        const Math::Vector3& e0 = keys[static_cast<int64_t>(perKey * k0)].translation;
        const Math::Vector3& e1 = keys[static_cast<int64_t>(perKey * k1)].translation;
        const float ax = e0.x;
        const float ay = e0.y;
        const float az = e0.z;
        const float bx = e1.x;
        const float by = e1.y;
        const float bz = e1.z;
        // a + (b - a) * t, in retail's association. NOT a*(1-t) + b*t.
        x = ax + (bx - ax) * frac;
        y = ay + (by - ay) * frac;
        z = az + (bz - az) * frac;
        ++rtgfxbody29::g_rootPoseLerped;
    }

    // The second Time scales the sampled translation. Same narrow-then-scale
    // order as the first.
    float scale = static_cast<float>(static_cast<double>(*t1) * secondsPerTick);
    scale *= rate;

    // Rotation is the IDENTITY quaternion: retail stores XMM4 (zeroed by
    // `XORPS XMM4,XMM4`) into +0x00/+0x04/+0x08 and the lifted 1.0f into +0x0c.
    ret->rotation.x = 0.0f;
    ret->rotation.y = 0.0f;
    ret->rotation.z = 0.0f;
    ret->rotation.w = rtgfxbody29::kOne;
    ret->translation.x = x * scale;
    ret->translation.y = y * scale;
    ret->translation.z = z * scale;
    (void)ret;
    return out;
}

}  // namespace Graphics

// Wave 28 defect #7: every lane's summary is written and NOTHING CALLS IT, so a
// zero means "never printed" rather than "never called". This one is WIRED —
// src/app/main.cpp calls it unconditionally beside the other Graphics lanes'
// — and the lane doc reports that it was seen to PRINT before any zero in it
// was believed.
void RtGraphicsWave29GfxbodySummary() {
    printf("[GFXBODY29] Scene::AddGlobalDrawItems calls=%llu visited=%llu emitted=%llu\n",
                (unsigned long long)rtgfxbody29::g_addGlobal,
                (unsigned long long)rtgfxbody29::g_addGlobalVisited,
                (unsigned long long)rtgfxbody29::g_addGlobalEmitted);
    printf("[GFXBODY29] Renderer::AddLight calls=%llu | "
                "Renderer::EndDrawGroup calls=%llu sortedLists=%llu\n",
                (unsigned long long)rtgfxbody29::g_addLight,
                (unsigned long long)rtgfxbody29::g_endDrawGroup,
                (unsigned long long)rtgfxbody29::g_endDrawSorted);
    printf("[GFXBODY29] Animation::GetRootPoseAtTime lerped=%llu single=%llu\n",
                (unsigned long long)rtgfxbody29::g_rootPoseLerped,
                (unsigned long long)rtgfxbody29::g_rootPoseSingle);
}
