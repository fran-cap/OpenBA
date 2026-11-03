// ============================================================================
// src/engine/collection/array.cpp — Wave 9 lane Q: Collection::Array<T>
// ============================================================================
//
// SCOPE: lane D's jobs J1 (`~Array`, 11 addresses) and J3 (`SetSize`, 16
// addresses) — 27 of the 66 unnamed DLL internals in
// docs/history/runtime_port/wave9_internals.csv, retired by TWO algorithms.
//
// `Collection::Array<T>` is a header-only template with ZERO exports in either
// shipped build (docs/history/runtime_port/WAVE9_LANE_D.md checked the profile DLL's
// 5,182-name export directory: no `Array@Collection` match). MSVC emitted one
// COMDAT per instantiation and the linker did not fold them, so what looks like
// 27 mysteries is two functions written 27 times. Every address below is
// therefore a `RegisterRuntimeAddr`, never a `RegisterRuntimeExport`: there is
// no name to register under.
//
// The layout — {_data +0x00, _size +0x08, _capacity +0x0c}, 16 bytes — is
// src/engine/collection/Collection.h, pinned there by four independent
// inlined bodies. This file re-uses that struct rather than restating it.
//
// ---------------------------------------------------------------------------
// WHAT WAS RE-VERIFIED (and what lane D got wrong)
// ---------------------------------------------------------------------------
// Lane D proved the clustering by hashing MNEMONIC SEQUENCES. Lane Q re-read
// all 27 bodies as disassembly (never the decompiled C —
// reference_ghidra_c_output_vs_disassembly.md: three Wave 6 defects including a
// crash came from Ghidra printing a MOVSXD as a zero extension, and `SetSize`
// contains two of them, at the `_size` load and the `_capacity * stride`
// multiply).
//
// J1 — CONFIRMED, and STRONGER than claimed. All eleven bodies are identical
//      instruction for instruction AND in every resolved absolute operand
//      (`0x1801fc718`, `0x18014b390`, `0x18014b388`, `0x1800c6c00`). Lane D said
//      "identical mnemonics and immediates, differing only in RIP-relative
//      displacements"; the displacements differ but they resolve to the same
//      four addresses. So J1 has NO parameters at all — not even a stride. One
//      body, eleven registrations, nothing to get wrong per instantiation.
//      (WAVE 9 LANE UIF: make that TWELVE registrations. A twelfth address,
//      0x180037240, was added below with the same byte-level evidence; the
//      "no parameters at all" conclusion is unchanged and is what lets one
//      body serve the extra address for free.)
//
// J3 — the ALGORITHM is one algorithm, but "16 instantiations parameterized by
//      stride / alignment / element-assign" is WRONG about the SIGNATURE, and
//      the signature is the part that corrupts memory if you get it wrong:
//
//        * EIGHT of the sixteen are TWO-ARGUMENT `SetSize(this, int n)` with no
//          `fill` parameter at all. Registering them behind a three-argument
//          typedef and dereferencing R8 reads a register the caller never set.
//        * ONE (0x18007b960) is a fully constant-folded ONE-argument call:
//          n = 10000 and the fill value -1 are both immediates in the body.
//        * THREE (0x180004e30, 0x180051390, 0x180061480) are NOT
//          trivially-copyable element types. They call unexported per-element
//          helpers, and they have a SHRINK-PATH DESTROY BRANCH that does not
//          exist in the other thirteen bodies at all. Porting them creates five
//          new lane-D rows (named in `kSpec*` below and in WAVE9_LANE_Q.md).
//        * TWO (0x180008b90, 0x18002fea0) leave the NEW TAIL UNINITIALISED —
//          the `else` arm of their fill loop is empty. A port that helpfully
//          zeroed it would not be faithful, and an oracle that compared it
//          would fail 100% of calls
//          (reference_oracle_must_not_compare_unwritten_bytes.md).
//
//      The full per-address table is `kSpec*` below and the parameterization
//      table in docs/history/runtime_port/WAVE9_LANE_Q.md.
//
// ---------------------------------------------------------------------------
// THE ALLOCATOR BOUNDARY — why this cluster is portable NOW
// ---------------------------------------------------------------------------
// The whole cluster's only external dependency is the allocator, and it reaches
// it through two EXPORTED statics that are byte-for-byte the sequences retail
// inlines here:
//
//   ?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z   0x1800c7a50
//       EnterCriticalSection(_singleton+0x640); Allocate(_singleton, n, align);
//       LeaveCriticalSection.
//   ?MemFree@MemoryAllocator@System@@SAXPEAX@Z      0x1800c7ad0
//       if (p) { EnterCriticalSection(_singleton+0x640);
//                Free(_singleton, p); LeaveCriticalSection; }
//
// 0x1800c7ad0 was disassembled for this lane and is INSTRUCTION FOR INSTRUCTION
// the tail of every J1 body, plus the null guard J1 performs itself. So calling
// it IS retail's body minus the inlining — and it keeps <windows.h> out of
// src/engine/ per PLAN.md §4.1, which is the reason
// src/engine/picking/scene.cpp had to leave one such free in the DLL.
//
// CALLED BY ADDRESS, NOT BY NAME, and that is deliberate. Going by name would
// make these buffers come from whichever allocator the name happened to resolve
// to while another body frees them into the other one. A heap cannot be
// half-ours. src/engine/graphics/scene.cpp:206 made the same call for the same
// reason and this file follows it.
//
// *** LANE E DEPENDENCY, STATED SO IT CANNOT BE MISSED. *** At cutover
// `ResolveAddr(Module::Runtime, 0x1800c7a50)` resolves nothing, because there is
// no DLL. These two accessors MUST be repointed at lane E's allocator in the
// same commit that arms it, and they are deliberately the only two places in
// this file that name a Runtime address other than the 27 being registered.
//
// ---------------------------------------------------------------------------
// ORACLE
// ---------------------------------------------------------------------------
// A destructor and an in-place resize cannot be replayed at a live call site:
// running ours then theirs frees the same buffer twice. The oracle for this
// lane is therefore a SYNTHETIC-SUBJECT replay in array_probe.cpp — two
// identical private Arrays, ours run on one and the shipped body on the other,
// the results compared — plus the cheap live checks below, which exist mostly
// to answer WHICH of the 27 addresses a given run actually reached.
//
// Read docs/history/runtime_port/WAVE9_LANE_Q.md "Oracle plan" before believing a green
// run: `calls=0` on a site reports clean and is NOT verified
// (reference_uncalled_port_is_not_verified.md), and one shared body serving
// eleven addresses means a run can verify the BODY while leaving ten
// REGISTRATIONS untouched.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "collection/Collection.h"
#include "array_internal.h"
#include "collection/setsize_prof.h"   // DRAWSORT: SetSizeCore's fast arm switch + COMPPROF counter
// B3R RT-Core: the element ops below call their bodies by name.
#include "system/Filename.h"      // PkgEntryAssign: the Filename copy ctor 0x1800067d0
#include "io/Modification.h"      // RtColl_ArrayU8Assign_180011220
#include "save/SaveLoad.h"        // Save::SaveLoad::SaveInfo (kSpec_MetaData58)
#include "graphics/Scene.h"       // RtGraphics_DrawGroup_CopyCtor 0x18001be60
#include "graphics/Animation.h"   // RtGraphics_Animation_CopyCtor / _Dtor
#include "graphics/Camera.h"      // Graphics::RendererDrawGroup (kSpec_DrawGroup1f0)
#include "system/MemoryAllocator.h"   // B3R: MemAlloc / MemFree by name
// B.4 (R-CORE): the element types the hand-derived rows below read by name.
#include "system/String.h"            // RtSystem::StringRefCount
#include "math/Math.h"                // Math::Transform
#include "ui/SpriteContext.h"         // UI::SpriteDraw
#include "ui/SpriteCapture.h"         // UI::CaptureEntry
#include "ui/UserInterface.h"         // UI::Event
#include "ui/Sprite.h"                // UI::Sprite
#include "ui/SpriteSheet.h"           // UI::SpriteInfo
#include "ui/Dialog.h"                // RtUI::DialogNamedTemplate / DialogControllerConfigEntry
#include "ui/StringTable.h"           // UI::StringTable::TextEntry
#include "ui/GraphElement.h"          // UI::GraphSeries
#include "ui/TableDescription.h"      // UI::TableDescription::Cell
#include "ui/LayoutDescription.h"     // UI::LayoutDescription::Entry
#include "video/VertexLayout.h"       // Video::VertexLayout::Element
#include "video/ShaderProgram.h"      // Video::ShaderProgram::Parameter
#include "io/Persistence.h"           // IO::Persistence::SaveGameInfo (RtIo::) / BlockDesc
#include "ui/DocumentTextElement.h"   // UI::DocumentTextElement::Link
#include "io/Stream.h"                // RtIo::CompressedStreamWindow
#include "io/FileSystem.h"            // RtIO::PackageFileEntry
#include "object/External.h"          // RtObject::ExternalArrayElement
#include "graphics/Material.h"        // RtGraphics::MaterialTexture
#include "graphics/AnimationGroup.h"  // Graphics::AnimationGroup::Tag
#include "audio/Audio.h"              // Audio::Scene::Channel / AttenutedEffect
#include "audio/SoundEffect.h"        // Audio::SoundEffect::Sample / Variant
#include "collision2d/CollisionMesh.h" // Collision2D::CollisionMesh::Face / Triple3
#include "picking/PickingMesh.h"      // Picking::PickingMesh::SubMesh / Triangle
#include "graphics/Particle.h"        // RtGraphics::ParticleExtra
#include "graphics/ParticleInstance.h" // RtGraphics::ParticleRecord

namespace rtcoll {

// The 16-byte container header (aliased in array_internal.h). sizeof(Array<T>)
// is T-independent and Collection.h static_asserts that; `uint8_t` is chosen so
// `_data` is already the byte cursor every body below wants.
static_assert(sizeof(ArrayHdr) == 0x10, "Collection::Array is 16 bytes");
static_assert(offsetof(ArrayHdr, _data)     == 0x00, "Array::_data @ +0x00");
static_assert(offsetof(ArrayHdr, _size)     == 0x08, "Array::_size @ +0x08");
static_assert(offsetof(ArrayHdr, _capacity) == 0x0c, "Array::_capacity @ +0x0c");

// ============================================================================
// The allocator boundary. See the banner for why these go by address.
// ============================================================================
// RETRIES A NULL rather than caching it, which is not defensive tidying: at
// cutover this address is supplied by RegisterAllocatorAddresses() inside the
// allocator's arming path, and ANY caller that ran before that point would
// otherwise freeze a null into this static for the life of the process and take
// the whole heap down with it. Same contract as RtResolveCached (rt_port.h):
// cache a hit, retry a miss. A hit costs one load, exactly as before.
#ifdef BANISHED_UNIT_TEST
// The host unit test (tests/unit/array_test.cpp) links this file without the
// address registry, so there is nothing at 0x1800c7a50 to resolve. The tests
// only need A heap and every allocation they make they also free — the same
// seam, for the same reason, that src/engine/system/string.cpp uses.
// Alignment is honoured by malloc's own 16-byte guarantee: no Array spec in
// the table asks for more than 8.
void* MemAlloc(uint64_t bytes, int align) {
    (void)align;
    return std::malloc(static_cast<size_t>(bytes));
}
#else
// ENDGAME B.3.2 closing pass: the resolution moved into `rtcoll::DoorAlloc`
// (alloc_door.cpp), which asks the exe dispatch slot 0x140152b60 first and only
// falls back to this address. Same allocator, but through the door the rest of
// the tree uses and the door `BANISHED_REDZONE` rewrites — see that file's
// banner, and Collection.h's `DoorAlloc` block, for why a container that
// resolved 0x1800c7a50 itself was invisible to the instrument. The call stays
// UNGUARDED, exactly as it was: a null here means the allocator never armed,
// and that is a crash worth having rather than a silent null buffer.
void* MemAlloc(uint64_t bytes, int align) {
    return System::MemoryAllocator::MemAlloc(bytes, align);
}
#endif

// `if (p) { Enter; Free(_singleton, p); Leave; }` — the null test is INSIDE
// 0x1800c7ad0 (`TEST RCX,RCX; JZ ret` is its first instruction), which is why
// the callers below hand it a possibly-null pointer exactly as retail does.
#ifdef BANISHED_UNIT_TEST
void MemFree(void* p) { std::free(p); }   // retail's null test lives in free()
#endif
// (B7 D4: outside the unit test MemFree is header-inline in Collection.h,
// System::MemoryAllocator::MemFree by name -- dispatch slot 0x140152b68 first,
// see MemAlloc.)

// ============================================================================
// J1 — Collection::Array<T>::~Array, trivially-destructible T
// ============================================================================
//
// Retail, read at 0x18001c750 and confirmed identical at the other ten:
//
//     PUSH RDI / SUB RSP,0x30 / MOV [RSP+0x20],-2      ; MSVC EH state, no
//     MOV [RSP+0x48],RBX / MOV [RSP+0x50],RSI          ; semantics
//     MOV RDI,RCX
//     MOV RSI,[RCX]                    ; p = this->_data
//     TEST RSI,RSI / JZ zero
//     MOV RBX,[0x1801fc718] / ADD RBX,0x640            ; &_singleton->cs
//     MOV RCX,RBX / CALL [EnterCriticalSection]
//     MOV RDX,RSI / MOV RCX,[0x1801fc718] / CALL 0x1800c6c00   ; Free
//     MOV RCX,RBX / CALL [LeaveCriticalSection]
//  zero:
//     XOR EAX,EAX
//     MOV [RDI],RAX                    ; _data     = 0
//     MOV [RDI+8],RAX                  ; _size AND _capacity = 0, as one qword
//     ... epilogue ... RET             ; RAX is 0 at RET
//
// Three things worth spelling out because a "cleanup" would break them:
//
//  * `[RDI+8]` is ONE eight-byte store covering _size and _capacity together.
//    Capacity IS reset here — unlike SetSize, which never shrinks it.
//  * RAX is zero at the RET. No caller in the tree reads it, but returning
//    `void*`/nullptr costs nothing and means a future caller that does cannot
//    diverge. (A `void` return would leave GCC's choice of RAX in the register,
//    which is a difference for free.)
//  * The `MOV [RSP+0x20],-2` and the `MOV [RSP+0x40],RBX` spill are the MSVC
//    unwind funclet's state slot and its `this` for the critical section. They
//    have no effect on the object and are not reproduced.
//
// NOT PARAMETERIZED. The eleven bodies are the same instructions with the same
// operands; the element stride never appears, because freeing a buffer does not
// need to know what is in it. This is why one C++ function serves all eleven.
// B6 row 104: the null test comes FIRST, as every J1 copy has it (0x18001c750,
// 0x18001c100: `MOV RSI,[RCX]; TEST RSI,RSI; JZ zero` before the
// Enter / Free / Leave triple), so a null buffer never reaches MemFree (and an
// allocator instrument on MemFree, BANISHED_REDZONE, never sees a null free).
// B7 D4: the body is header-inline in src/engine/collection/Collection.h
// (`rtcoll::ArrayDtorImpl`), unchanged, so a typed Array local open-codes it.

// ============================================================================
// J3 — Collection::Array<T>::SetSize
// ============================================================================
//
// Retail, read at 0x18007a0d0 / 0x180023830 / 0x18002fea0 and cross-checked
// against the other thirteen. In C, with the parameters named:
//
//   void SetSize(Array* a, int n, const T* fill) {
//       int64_t oldSize = (int32_t)a->_size;            // MOVSXD  <-- (1)
//       if (n == a->_size) return;                      // exact-equal no-op
//       T* old = a->_data;
//       a->_size = n;
//       if (n > a->_capacity) {                         // SIGNED compare
//           a->_capacity = n;
//           if (n == (int32_t)oldSize + 1 && oldSize != 0)
//               a->_capacity = n * 3;                   // 32-bit LEA
//           a->_data = MemAlloc((int64_t)a->_capacity * stride, align); // (2)
//       }
//       int64_t i = (old == a->_data) ? oldSize : 0;
//       for (; i < n; ++i) {
//           T* dst = a->_data + i;
//           if (dst == 0) continue;                     // yes, really
//           if (i < oldSize) ASSIGN(dst, old + i);
//           else             DEFAULT(dst) /* or ASSIGN(dst, fill) */;
//       }
//       if (old != a->_data) { DESTROY(old, oldSize); MemFree(old); }
//       else if (oldSize > n) DESTROY(old + n, oldSize - n);
//   }
//
// (1) and (2) are the two MOVSXDs Ghidra's C prints as zero extensions. They
// only matter for negative values, and a negative `_size` or `_capacity` is
// already a corrupt container — but they are written as sign extensions here
// because the point of a faithful port is that it fails the same way.
//
// Details that are easy to lose and are all load-bearing:
//
//  * The no-op test is `n == _size`, not `n <= _capacity`. SetSize(_size) does
//    nothing at all, including no fill.
//  * Capacity NEVER shrinks: the shrink path skips the whole `if` and the copy
//    loop (start = oldSize >= n), so the buffer is kept.
//  * The `* 3` treble is gated on `oldSize != 0`, so the FIRST append to an
//    empty array allocates capacity exactly 1. Appending one at a time from
//    empty therefore walks 1, 6, 21, 66, 201 — and a bulk SetSize(n) allocates
//    exactly n, because n != oldSize + 1.
//  * `if (dst == 0)` is a per-iteration test of the COMPUTED element address.
//    It can only fire when `_data` is null and i == 0, i.e. when MemAlloc
//    failed. Retail checks it every iteration and so does this.
//  * The copy source cursor advances by `stride` every iteration whether or not
//    the copy branch is taken, which matters only if you write the loop with a
//    separate pointer; it is written as `old + i * stride` here, which is the
//    same thing.
//
// ---------------------------------------------------------------------------
// The parameterization
// ---------------------------------------------------------------------------
// `stride` and `align` are what the MemAlloc call site bakes in
// (`SHL RCX,3` + `MOV EDX,8`, or `IMUL RCX,RCX,0x1c` + `MOV EDX,4`, ...).
// `assign` / `init` / `destroy` are the element operations. `init` is used ONLY
// by the two-argument form; a null `init` means retail's `else` arm is EMPTY
// and the new tail is left holding whatever the allocation contained.
// ============================================================================

// `ElemAssign` / `ElemInit` / `ElemDestroy` and `ArraySpec` are declared in
// array_internal.h so the probe drives the identical rows.
//
// ElemDestroy's first parameter is `ignoredThis`: retail spills RCX to the
// shadow space and never reads it in either destroy helper. Verified by
// disassembling 0x1800059e0 and 0x180005930 — both start
// `TEST R8D,R8D / JLE ret / MOV [RSP+8],RCX` — and the only caller of
// 0x1800059e0 (inside 0x180004e30) does not set RCX at all before the call.

// ---- the String adopt every refcounted row shares ---------------------------
// Retail's inlined `System::String` copy: the slot is zeroed FIRST, then, if the
// source holds a string, it is taken and its refcount (`INC [str-4]`) bumped.
// The unconditional zero is why a null source leaves the slot null rather than
// untouched (AssignStringRef's banner below).
static inline void AdoptString(char*& slot, char* src) {
    slot = nullptr;
    if (src) {
        slot = src;
        ++RtSystem::StringRefCount(src);
    }
}

// The shared word ops (B13: one body serving several element types).
static void ZeroThenCopy8(uint64_t* d, const uint64_t* s) { *d = 0; *d = *s; }
template <class W, int N>
static void ZeroWords(W* d) { for (int k = 0; k < N; ++k) d[k] = 0; }
static void NegOne32(int32_t* d) { *d = -1; }

// ---- trivially-copyable element operations ---------------------------------

void Assign1 (uint8_t* d, const uint8_t* s) { *d = *s; }
void Assign4 (uint8_t* d, const uint8_t* s) { AssignAs<uint32_t, &CopyWords<uint32_t, 1>>(d, s); }
void Assign8 (uint8_t* d, const uint8_t* s) { AssignAs<uint64_t, &CopyWords<uint64_t, 1>>(d, s); }
// 0x180008d60 / 0x1800764c0 / 0x180076900 / 0x18007a0d0 store a ZERO first and
// then the value, into the same eight bytes. That is the smart-pointer element
// type's `operator=` inlined (null the slot, then take the new referent), and
// it is reproduced rather than folded: the final state is identical, but a
// hardware write watchpoint or a concurrent reader would see the difference,
// and BANISHED_REDZONE=watch is one of this lane's instruments.
void Assign8Z(uint8_t* d, const uint8_t* s) { AssignAs<uint64_t, &ZeroThenCopy8>(d, s); }
void Assign16(uint8_t* d, const uint8_t* s) { AssignAs<uint64_t, &CopyWords<uint64_t, 2>>(d, s); }
// 0x180059140, Math::Transform: seven dwords at +0,+4,...,+0x18.
void Assign1c(uint8_t* d, const uint8_t* s) { AssignAs<uint32_t, &CopyWords<uint32_t, 7>>(d, s); }

// WAVE 9 LANE PKG2 rung 4b-ii — 0x180024c10, Array<CompressedStream::Window>.
// The copy is three plain qword moves at +0x00, +0x08 and +0x10.
void Assign18(uint8_t* d, const uint8_t* s) { AssignAs<uint64_t, &CopyWords<uint64_t, 3>>(d, s); }

// WAVE 9 LANE UIF — 0x180078520 / 0x1800783d0, Array<UI::SpriteDraw>, stride
// 0x28. The copy at 0x1800785c2..0x1800785e4 is FIVE plain qword moves at
// +0x00, +0x08, +0x10, +0x18 and +0x20: no zero-first store, no refcount, so
// the assign arm is a pure byte copy covering the whole 0x28-byte element.
void Assign28(uint8_t* d, const uint8_t* s) { AssignAs<uint64_t, &CopyWords<uint64_t, 5>>(d, s); }

// WAVE 25 LANE INTERNAL42 — 0x18002a810, the Persistence block directory.
// The copy at 0x18002a89d..0x18002a8b2 is THREE plain dword moves at +0x00,
// +0x04 and +0x08, written out longhand by MSVC rather than looped. Stride is
// `LEA RCX,[RAX+RAX*2]; SHL RCX,2` = 0xc, so the element is exactly the three
// dwords and there is no tail padding for a fourth store to hide in.
void Assign12(uint8_t* d, const uint8_t* s) { AssignAs<uint32_t, &CopyWords<uint32_t, 3>>(d, s); }

void InitZero8(uint8_t* d) { InitAs<uint64_t, &ZeroWords<uint64_t, 1>>(d); }
// 0x18007b960's `else` arm is `MOV dword ptr [RDX],0xffffffff`.
void InitNeg1 (uint8_t* d) { InitAs<int32_t, &NegOne32>(d); }
// WAVE 9 LANE UIF — the default (no-fill) arm of Array<UI::SpriteDraw>, read
// out of 0x1800783d0 at 0x18007847b..0x180078494. SIX stores, and what they
// DO NOT cover is the load-bearing part:
//     MOV [RAX],RBP        +0x00 qword = 0   sheet
//     MOV [RAX+0x8],RBP    +0x08 qword = 0   spriteIndex, x1
//     MOV [RAX+0x10],RBP   +0x10 qword = 0   y1, x2
//     MOV [RAX+0x18],EBP   +0x18 dword = 0   y2
//     MOV [RAX+0x20],EBP   +0x20 dword = 0   color
//     MOV [RAX+0x24],BPL   +0x24 byte  = 0   isLine
// so +0x1c..+0x1f (`rotation`) and +0x24+1..+0x27 (the tail padding) are
// NEVER WRITTEN by this arm. That is the retail default-constructed
// UI::SpriteDraw: every member zeroed except a float that has no initialiser.
static void SpriteDrawInit(UI::SpriteDraw* d) {
    d->sheet       = nullptr;  // +0x00 qword
    d->spriteIndex = 0;        // +0x08 qword (spriteIndex, x1)
    d->x1          = 0;
    d->y1          = 0;        // +0x10 qword (y1, x2)
    d->x2          = 0;
    d->y2          = 0;        // +0x18 dword
    d->color       = 0;        // +0x20 dword
    d->isLine      = 0;        // +0x24 byte
}
void InitSpriteDraw(uint8_t* d) { InitAs<UI::SpriteDraw, &SpriteDrawInit>(d); }
// 0x180059140's `else` arm, the identity Math::Transform:
//   [d+0x00] = 0 (qword)  [d+0x08] = 0 (dword)
//   [d+0x0c] = 0x3f800000 (qword: 1.0f at +0x0c, 0 at +0x10)
//   [d+0x14] = 0 (qword)
// All 0x1c bytes are written, so there is no unwritten hole in this one.
// 0x180024c10's `else` arm, an empty CompressedStream::Window. Three stores,
// and TWO of them are worth spelling out:
//   MOV dword ptr [RCX],R11D        -> [d+0x00] = 0   (4 bytes)
//   MOV qword ptr [RCX+0x4],-0x1    -> [d+0x04] = -1  (8 bytes, UNALIGNED,
//                                      covering +0x04..+0x0b)
//   MOV qword ptr [RCX+0x10],R11    -> [d+0x10] = 0   (8 bytes)
// So **+0x0c..+0x0f is NEVER WRITTEN** by the tail init, even though the
// element stride is 0x18. Any oracle over a grown Array<Window> must exclude
// those four bytes or it compares memory neither side wrote
// (reference_oracle_must_not_compare_unwritten_bytes). The unaligned 8-byte -1
// is retail's, not a transcription convenience — the same shape as
// InitIdentityTransform's qword at +0x0c below.
static void WindowInit(RtIo::CompressedStreamWindow* d) {
    d->useCount = 0;             // DWORD
    d->start    = -1;            // the UNALIGNED qword of -1 over +0x04..+0x0b:
    d->end      = -1;            //   start and end both -1 (H7)
    d->buffer   = nullptr;       // +0x10 qword
}
void InitWindow(uint8_t* d) { InitAs<RtIo::CompressedStreamWindow, &WindowInit>(d); }

static void IdentityTransformInit(Math::Transform* d) {
    d->rotation.x    = 0.0f;   // +0x00 qword (x, y)
    d->rotation.y    = 0.0f;
    d->rotation.z    = 0.0f;   // +0x08 dword
    d->rotation.w    = 1.0f;   // +0x0c qword 0x3f800000 (w = 1.0f, translation.x = 0)
    d->translation.x = 0.0f;
    d->translation.y = 0.0f;   // +0x14 qword (translation.y, .z)
    d->translation.z = 0.0f;
}
void InitIdentityTransform(uint8_t* d) { InitAs<Math::Transform, &IdentityTransformInit>(d); }

// ---------------------------------------------------------------------------
// UI leftovers batch (docs/history/runtime_port/closure/WORKLIST.md) — three more
// element types, read from the disassembly of 0x18004ba90/0x18004bde0
// (`UI::SpriteCapture::_entries`, element `UI::SpriteCapture::CaptureEntry`
// per src/engine/ui/SpriteCapture.h), 0x180033690 (`UI::UserInterface::
// _events`, element `UI::UserInterface::Event`) and 0x1800370c0
// (`UI::SpriteCapture::_sprites` / `UI::UserInterface`'s fade sprite family,
// element `UI::Sprite`, {ExternalPtr<SpriteSheet>, int32 index, pad}).
// ---------------------------------------------------------------------------

// `CaptureEntry` (0x28 bytes): sheetIndex(+0x00,i32), pad04(+0x04, never
// touched by either instantiation), name(+0x08, System::String, refcounted),
// x1/y1/x2/y2(+0x10..+0x1f, i32 each), rotation(+0x20,f32), color(+0x24,u32).
// SpriteCapture.h's banner already worked out that both 0x18004ba90 (the
// two-argument SetSize) and 0x18004bde0 (the three-argument SetSize with a
// fill) COPY-CONSTRUCT this element -- the name slot is zeroed first, then the
// source pointer is copied and its refcount bumped, exactly like
// `ReleaseStringsCore`'s release is the mirror of this acquire. Read out of
// 0x18004be97..0x18004bed0 (the 3-arg body) and cross-checked against
// 0x18004ba90's own copy loop, which is instruction-for-instruction the same
// shape at a different source pointer (the OLD element during a realloc move,
// rather than the caller's `fill`).
static void CaptureEntryAssign(UI::CaptureEntry* d, const UI::CaptureEntry* s) {
    d->sheetIndex = s->sheetIndex;          // +0x00
    // +0x04 pad -- neither instantiation writes it.
    AdoptString(d->name._data, s->name._data);  // +0x08, INC [str-4]
    d->x1       = s->x1;
    d->y1       = s->y1;
    d->x2       = s->x2;
    d->y2       = s->y2;
    d->rotation = s->rotation;
    d->color    = s->color;
}
void AssignCaptureEntry(uint8_t* d, const uint8_t* s) {
    AssignAs<UI::CaptureEntry, &CaptureEntryAssign>(d, s);
}
// 0x18004ba90's `else` (init) arm, the two-argument form's default-constructed
// tail: FOUR qword zero stores at +0x08, +0x10, +0x18, +0x20. +0x00 (sheetIndex)
// and +0x04 (pad) are NEVER written by this arm -- a real gap, not an omission
// here: the disassembly's else-branch starts at `lVar5+8`, never `lVar5`.
static void CaptureEntryInitTail(UI::CaptureEntry* d) {
    d->name._data = nullptr;                // +0x08 qword
    d->x1       = 0;                        // +0x10 qword (x1, y1)
    d->y1       = 0;
    d->x2       = 0;                        // +0x18 qword (x2, y2)
    d->y2       = 0;
    d->rotation = 0.0f;                     // +0x20 qword (rotation, color)
    d->color    = 0;
}
void InitCaptureEntryTail(uint8_t* d) {
    InitAs<UI::CaptureEntry, &CaptureEntryInitTail>(d);
}

// `UI::UserInterface::Event` (0x30 bytes), the array UserInterface.h left
// deliberately opaque. 0x180033690's copy loop, read field by field: +0x00 and
// +0x08 are "zero-then-copy" qwords (a smart-pointer-shaped store, but NO
// refcount INC anywhere in this body -- unlike CaptureEntry's name slot, so
// these are raw pointers with no owned reference), +0x10/+0x14/+0x18/+0x1c/
// +0x20 are five plain dword copies, and +0x28 is a plain qword copy. That
// leaves **+0x24..+0x27 (4 bytes) NEVER WRITTEN** -- the one gap in an
// otherwise fully-covered 0x30-byte element.
static void EventAssign(UI::Event* d, const UI::Event* s) {
    d->dialog   = nullptr;
    d->dialog   = s->dialog;
    d->element  = nullptr;
    d->element  = s->element;
    d->eventId  = s->eventId;
    d->arg14    = s->arg14;
    d->arg18    = s->arg18;
    d->arg1C    = s->arg1C;
    d->arg20    = s->arg20;
    // +0x24..+0x27 deliberately untouched -- retail does not write it either.
    d->userData = s->userData;
}
void AssignEvent30(uint8_t* d, const uint8_t* s) { AssignAs<UI::Event, &EventAssign>(d, s); }

// 0x1800370c0's `else` (init) arm, the two-argument UI::Sprite clear: the
// ExternalPtr<SpriteSheet> qword at +0x00 goes to null and the index dword at
// +0x08 goes to -1 (`MOV [d],0; MOV dword[d+8],0xffffffff`), matching
// UI::Sprite's documented default (UserInterface.h: "index = -1 is the
// engine's empty-sprite value"). +0x0c (pad) is never written.
static void SpriteInitEmpty(UI::Sprite* d) {
    d->spriteSheet = nullptr;
    d->spriteIndex = -1;
}
void InitSpriteIndexNeg1(uint8_t* d) { InitAs<UI::Sprite, &SpriteInitEmpty>(d); }

// ============================================================================
// The three NON-TRIVIAL element types (0x180004e30, 0x180051390, 0x180061480)
// ============================================================================
//
// These are the ones lane D's "one algorithm parameterized by stride,
// alignment and element-assign" did not cover: their element operations are
// CALLS to unexported helpers, and they carry a shrink-path destroy branch the
// other thirteen bodies do not have at all.
//
// Porting them the faithful way — calling the helpers at their own addresses,
// exactly as ClearAll / KeyMap::RegisterInput / Audio::Scene::Serialize already
// call Collection internals by address — CREATES FIVE NEW LANE-D ROWS. They are
// named here so the next census does not have to rediscover them:
//
//   0x180004220   element copy-assign for the 0x38-byte SpriteSheet element
//   0x1800059e0   destroy-range for the same (releases two String refs each)
//   0x180051650   element copy-assign for Audio's 0x40-byte EffectSlot
//   0x180052110   destroy-range for the same
//   0x180005930   destroy-range for the 0x10-byte {String*, ExternalPtr}
//
// *** CORRECTED 2026-09-07 BY WAVE 9 LANE D2. *** THREE OF THOSE FIVE ROWS NO
// LONGER EXIST: 0x1800059e0, 0x180052110 and 0x180005930 are ported and
// registered by src/engine/collection/array_destroy.cpp, which found that all
// eleven of this cluster's per-element DESTROY helpers are three bodies
// parameterized by (stride, offset-list). The three `ResolveAddr` calls below
// therefore reach OUR bodies now, not the DLL's — the registry is consulted
// first — and this file's `destroy` fields are satisfied without leaving the
// port.
//
// *** CORRECTED AGAIN 2026-09-08 BY WAVE 9 LANE COLL. ONE OF THE TWO REMAINING
// ROWS IS GONE AND THE REASON GIVEN FOR HOLDING IT WAS WRONG. *** 0x180051650
// is PORTED (src/engine/collection/introsort.cpp) and so is the callee this
// comment described as an Audio holdback: **0x180050d30 is not Audio-specific
// at all**, it is a plain `Collection::Array<16-byte POD>::operator=` — the
// same body lane UIW ported at 0x18001cdf0, with stride 0x10, ALIGN 4 (not 8,
// and not 0x10; that immediate is the whole difference and this family has now
// produced several near-misses that turned on one) and dtor 0x180051d80, which
// this file already registers at line ~1343. Its only other callee is MemAlloc.
// It was ALSO a separate unregistered address door in its own right, at
// src/engine/audio/audio.cpp:1114, and that row closes too.
//
// 0x180004220 (the 0x38-byte SpriteSheet element copy-assign) IS still the
// DLL's and is the last row of this cluster.
//
// The two ResolveAddr sites stay written exactly as they are: they resolve
// lazily, on first USE rather than at init, so they pick up lane D2's
// registrations with no ordering requirement between the two inits, and
// spelling the address as a literal is what keeps tools/runtime_census.py able
// to see the edge.
// ============================================================================

// B3R RT-Core: every element op below that is a CALL in retail calls its
// ported body BY NAME; the retail address stays on the line as a comment.

// 0x180004e30's copy: `MOV RCX,dst; MOV RDX,src; CALL 0x180004220`.
void Assign38Call(uint8_t* d, const uint8_t* s) {
    RtColl_SpriteEntryAssign_180004220(d, s);
}
// 0x180078950's `else` (init) arm — see kSpec_Sprite38Z for the transcription
// and for why this arm exists here and not on kSpec_Sprite38.
static void SpriteInfoInit(UI::SpriteInfo* d) {
    d->name._data  = nullptr;   // +0x00
    d->group._data = nullptr;   // +0x08
    d->x           = 0;         // +0x10 qword (x, y)
    d->y           = 0;
    d->_reserved18 = 0;         // +0x18 qword (_reserved18, width)
    d->width       = 0;
    d->height      = 0;         // +0x20 qword (height, u0)
    d->u0          = 0.0f;
    d->v0          = 0.0f;      // +0x28 qword (v0, u1)
    d->u1          = 0.0f;
    d->v1          = 0.0f;      // +0x30 DWORD, not a qword
    /* +0x34..+0x37 deliberately untouched — retail does not write them */
}
void InitSprite38Z(uint8_t* d) { InitAs<UI::SpriteInfo, &SpriteInfoInit>(d); }
void Destroy38(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_1800059e0(ignoredThis, base, count);
}

// UI leftovers batch: `Array<CaptureEntry>`'s shrink/realloc destroy, one
// String release at +0x08 per element (stride 0x28). Ported and registered by
// src/engine/collection/array_destroy.cpp (kD_Str_28at08, B_ENTRY(18003f070,
// ...)); reached through ResolveAddr so the registry-first rule
// (reference_resolveaddr_consults_port_registry_first.md) picks up OUR body
// regardless of file init order.
void Destroy28Call(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_18003f070(ignoredThis, base, count);
}

// 0x180051390's copy: `CALL 0x180051650` with RCX=dst, RDX=src.
void Assign40Call(uint8_t* d, const uint8_t* s) {
    rtsort::EffectSlotAssign(d, s);
}
void Destroy40(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyInner_180052110(ignoredThis, base, count);
}
// 0x180051390's `else` arm, inlined in retail (Audio::EffectSlot's default):
//   [+0x00]=0q [+0x08]=0q [+0x10]=0q [+0x18]=0q [+0x20]=0q
//   [+0x28]=0x3f800000 (DWORD)      [+0x30]=0q [+0x38]=0q
//
// *** BYTES +0x2c .. +0x2f ARE NEVER WRITTEN. *** The element is 0x40 bytes and
// this covers 0x00-0x2b and 0x30-0x3f. Any oracle that memcmps a whole new
// EffectSlot will fail on four bytes of allocator fill and read like a
// catastrophic port defect — that is exactly the Wave 6 failure recorded in
// reference_oracle_must_not_compare_unwritten_bytes.md, four times over. The
// probe excludes that window explicitly; see array_probe.cpp.
static void EffectSlotInit(Audio::Scene::AttenutedEffect* d) {
    d->pos.x               = 0.0f;   // +0x00 qword
    d->pos.y               = 0.0f;
    d->pos.z               = 0.0f;   // +0x08 qword (pos.z, soundType)
    d->soundType           = 0;
    d->soundEffectRef      = nullptr;            // +0x10
    d->startTime._ticks    = 0;                  // +0x18
    d->channelHandle       = 0;                  // +0x20 qword (channelHandle, idTag)
    d->idTag               = 0;
    d->baseVolume          = 1.0f;               // +0x28 DWORD 0x3f800000
    /* +0x2c .. +0x2f deliberately untouched — retail does not write them */
    d->positions._data     = nullptr;            // +0x30
    d->positions._size     = 0;                  // +0x38 qword (size, capacity)
    d->positions._capacity = 0;
}
void InitEffectSlot(uint8_t* d) { InitAs<Audio::Scene::AttenutedEffect, &EffectSlotInit>(d); }

// 0x180061480's copy, INLINED in retail rather than called. The element is
// {System::String* str; ExternalPtr ext} and the assignment is:
//     [dst]   = 0
//     RAX     = *(void**)src
//     if (RAX) { [dst] = RAX; ++*(int32_t*)((char*)RAX - 4); }   // REFCOUNT
//     [dst+8] = *(void**)(src+8)
// The `INC dword ptr [RAX-4]` is System::String's reference count at `str-4`
// (src/engine/system/string.cpp names the same convention). Getting this wrong
// leaks or double-frees strings far from here, so it is written out literally
// rather than as a 16-byte copy.
static void MaterialTextureAssign(RtGraphics::MaterialTexture* d,
                                  const RtGraphics::MaterialTexture* s) {
    AdoptString(d->name._data, s->name._data);   // REFCOUNT
    d->image = s->image;
}
void AssignStringExt(uint8_t* d, const uint8_t* s) {
    AssignAs<RtGraphics::MaterialTexture, &MaterialTextureAssign>(d, s);
}
void DestroyStringExt(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_180005930(ignoredThis, base, count);
}

// ============================================================================
// WAVE 9 LANE UIW — two more element types, both read out of their own bodies
// ============================================================================
//
// These exist because four cutover residue rows in UI (`ComboElement::AddItem`,
// `ComboElement::RemoveAllItems`, `GraphElement::AddGraph`,
// `RadioElement::FindCheckBoxes`) reach seven `Collection::Array` instantiations
// that no lane owned. Three of the seven turned out to be exact clones of specs
// this file already had; the two below are new, and the seventh is `~Array`.
//
// ---------------------------------------------------------------------------
// 1. Array<System::String>, 0x180034860
// ---------------------------------------------------------------------------
// The assign, read at 0x1800348fc..0x180034916 (NOT from decompiled C — the
// null-source arm is a branch Ghidra's C folds away):
//
//     MOV [R8],R15          ; dst = 0, unconditionally, FIRST
//     MOV RCX,[RAX]         ; src value
//     TEST RCX,RCX / JZ done
//     MOV [R8],RCX
//     INC dword ptr [RCX-4] ; System::String's refcount at str-4
//
// The unconditional `dst = 0` before the test is why a null source leaves the
// destination NULL rather than untouched: the whole 8-byte stride is written on
// both arms, so there is no unwritten window.
// --- WAVE 26 LANE INT33 -----------------------------------------------------
// 0x180075d20's assign arm, read at 0x180075dc4..0x180075ddb. It is the MIRROR
// of AssignStringExt above and NOT a reuse of it: that one has the String at
// +0x00 and the verbatim qword at +0x08; this one has them the other way round.
// The two are indistinguishable by stride, by alignment and by element size,
// and swapping them would refcount-bump whatever integer happens to sit at
// +0x00 and then leak the real String -- the same offset-vs-stride confusion
// array_destroy.cpp's kD_Str_10 / kD_Str_10at08 pair exists to prevent.
//
//   180075dc4  MOV RAX,[RDX-0x8] / MOV [RCX],RAX     +0x00 copied VERBATIM,
//                                                    and copied FIRST
//   180075dcb  MOV [RCX+0x8],R14  (R14 = 0)          +0x08 zeroed
//   180075dcf  MOV RAX,[RDX] / TEST RAX,RAX / JE
//   180075dd7  MOV [RCX+0x8],RAX / INC [RAX-0x4]     refcounted String
//
// The unconditional zero store at +0x08 BEFORE the test is why this arm covers
// the whole 0x10 stride even when the source String is null, and why the assign
// window is 0/0 rather than +0x08..+0x10.
static void NamedTemplateAssign(RtUI::DialogNamedTemplate* d, const RtUI::DialogNamedTemplate* s) {
    d->_description = s->_description;      // verbatim, and first
    AdoptString(d->_name._data, s->_name._data);
}
void AssignExtStringAt08(uint8_t* d, const uint8_t* s) {
    AssignAs<RtUI::DialogNamedTemplate, &NamedTemplateAssign>(d, s);
}
// 0x18002ae20's assign is the same instructions over IO::Persistence::
// SaveGameInfo {sortKey, name}: the row below names its own element.
static void SaveGameInfoAssign(RtIo::SaveGameInfo* d,
                               const RtIo::SaveGameInfo* s) {
    d->sortKey = s->sortKey;                // verbatim, and first
    AdoptString(d->name._data, s->name._data);
}
void AssignSaveGameInfo(uint8_t* d, const uint8_t* s) {
    AssignAs<RtIo::SaveGameInfo, &SaveGameInfoAssign>(d, s);
}
// 0x180075d20's `else` (init) arm, 0x180075dee..0x180075df1: two qword zero
// stores, so the whole 0x10 stride is written and there is no init window.
void InitZero10(uint8_t* d) { InitAs<uint64_t, &ZeroWords<uint64_t, 2>>(d); }
// 0x180075d20's shrink/realloc destroy: `CALL 0x180039640`, Body B with
// kD_Str_10at08. Ported AND registered in array_destroy.cpp in the same commit
// (it was one of "the eight ported but unregistered", and this is the caller
// that file's banner says to wait for), so this ResolveAddr is registry-first
// and lands on our body rather than opening a door onto an unported callee.
void DestroyExtStringAt08(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_180039640(ignoredThis, base, count);
}

// --- WAVE 26 LANE INT33: 0x180075b20's three element ops --------------------
// Element `{ int32 +0x00, HOLE +0x04..+0x07, qword +0x08, qword +0x10,
// System::String* +0x18 }`, stride 0x20. Read at 0x180075bc8..0x180075bed; the
// source cursor sits at src+0x18, so the displacements below are the listing's
// RDX-relative ones resolved to element-relative:
//
//   180075bc8  MOV EAX,[RDX-0x18] / MOV [RCX],EAX        +0x00  DWORD
//   180075bcd  MOV RAX,[RDX-0x10] / MOV [RCX+0x8],RAX    +0x08
//   180075bd5  MOV RAX,[RDX-0x8]  / MOV [RCX+0x10],RAX   +0x10
//   180075bdd  MOV [RCX+0x18],R14 (R14 = 0)              +0x18 zeroed first
//   180075be1  MOV RAX,[RDX] / TEST / MOV [RCX+0x18],RAX / INC [RAX-0x4]
//
// *** +0x04..+0x07 IS NEVER WRITTEN BY EITHER ARM. *** The first store is a
// DWORD and the next is at +0x08. The init arm (0x180075c00..0x180075c0b)
// makes the same four stores in zeroes -- `MOV dword [RAX],R14D` then qwords at
// +0x08/+0x10/+0x18 -- and skips the same hole. So the window is 0x04..0x08 on
// BOTH arms, and each was read separately rather than one assumed from the
// other (reference_never_written_needs_by_whom.md: the extent belongs to the
// ARM). An oracle that compared those four bytes would fail 100% of calls.
static void CfgEntryAssign(RtUI::DialogControllerConfigEntry* d,
                           const RtUI::DialogControllerConfigEntry* s) {
    d->_typeId      = s->_typeId;           // +0x00 DWORD
    /* +0x04..+0x07 deliberately not copied */
    d->_elementDesc = s->_elementDesc;      // +0x08
    d->_config      = s->_config;           // +0x10
    AdoptString(d->_name._data, s->_name._data);  // +0x18 zeroed first
}
void AssignCfgEntry20(uint8_t* d, const uint8_t* s) {
    AssignAs<RtUI::DialogControllerConfigEntry, &CfgEntryAssign>(d, s);
}
static void CfgEntryInit(RtUI::DialogControllerConfigEntry* d) {
    d->_typeId      = 0;                    // DWORD, not a qword
    /* +0x04..+0x07 deliberately untouched */
    d->_elementDesc = nullptr;
    d->_config      = nullptr;
    d->_name._data  = nullptr;
}
void InitCfgEntry20(uint8_t* d) { InitAs<RtUI::DialogControllerConfigEntry, &CfgEntryInit>(d); }
// CALL 0x180039580 — Body B with kD_Str_20at18, ported and registered by
// array_destroy.cpp in this same commit for the same reason as 0x180039640.
void DestroyCfgEntry20(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_180039580(ignoredThis, base, count);
}

// --- WAVE 27 LANE FRONTIER: 0x18002c6a0's two element ops -------------------
//
// ASSIGN, read at 0x18002c76d..0x18002c77d:
//     18002c76d  MOV RDX,RBX        src = old + i*0x10
//     18002c770  MOV RCX,R14        dst = new + i*0x10
//     18002c773  CALL 0x1800067d0   System::Filename's copy ctor
//     18002c779  MOV RAX,[RBX+0x8]
//     18002c77d  MOV [R14+0x8],RAX  the two int32s, copied as ONE qword
//
// *** IT DELEGATES TO 0x1800067d0 RATHER THAN INLINING THE STRING IDIOM, AND
// THAT IS THE TRAP THIS FAMILY HAS NOW SPRUNG FIVE TIMES. *** `AssignPkgEntry`
// above records the fourth: 0x1800067d0's LAST TWO ACTIONS are two calls to
// `Filename::AdjustDelimiters` (0x18000682a, 0x180006832 - both re-read from
// that body for this row rather than inherited from that note), so an element
// assign hand-written as "null the slot, adopt, incref" is byte-identical for
// the first eight bytes and leaves every dependency PATH with its separators
// unnormalised. That breaks lookup for any dependency spelled with forward
// slashes and breaks NOTHING otherwise - i.e. it passes a green run. This is
// exactly the shape WAVE26_CLOSED.md section 6 item 1 warned would "compile,
// run, and leak or double-release a refcount" with no byte oracle able to see
// it.
//
// By ADDRESS, not as a direct call, for the reason `AssignGraphSeries` records:
// ResolveAddr consults the port registry first, so the edge stays visible to
// tools/addr_door_audit.py and tools/runtime_census.py.
// --- WAVE 27 LANE FRONTIER: 0x180078c90's element ops -----------------------
//
// `Array<UI::StringTable::TextEntry>::SetSize(this, int)` - the array
// `?Serialize@StringTable@UI@@` resizes on the READ path (0x18011d9b7).
//
// The assign is INLINED in retail, not a call, and it is the refcount-adopt
// idiom run TWICE, read at 0x180078d33..0x180078d55:
//     MOV [RAX],R14        (R14 = 0)      null dst +0x00 FIRST
//     MOV RDX,[RCX] ; TEST ; MOV [RAX],RDX ; INC [RDX-0x4]
//     MOV [RAX+0x8],R14                   null dst +0x08
//     MOV RDX,[RCX+0x8] ; TEST ; MOV [RAX+0x8],RDX ; INC [RDX-0x4]
// Both slots are zeroed before the adopt, and the whole 0x10 is written, so
// there is no unwritten window on this arm.
//
// NOTE THE ORDER DIFFERENCE FROM THE DESTROY, which is not a contradiction and
// is worth stating because it looks like one: the assign walks +0x00 then
// +0x08, the destroy (0x180005570) walks +0x08 then +0x00. Each was read from
// its own body; neither was inferred from the other.
static void TextEntryAssign(UI::StringTable::TextEntry* d, const UI::StringTable::TextEntry* s) {
    AdoptString(d->name._data, s->name._data);  // 180078d33
    AdoptString(d->text._data, s->text._data);  // 180078d44
}
void AssignStringPair10(uint8_t* d, const uint8_t* s) {
    AssignAs<UI::StringTable::TextEntry, &TextEntryAssign>(d, s);
}

// The range destroy 0x180078c90 calls on BOTH arms - realloc (0x180078d82,
// count = oldSize) and shrink-tail (0x180078ddb, base = old + newSize*0x10).
// PORTED AND REGISTERED in array_destroy.cpp in this same commit as
// `RtColl_DestroyStrings_180005570` under the NEW `kD_Str2_10` row: it releases
// TWO slots per element, and reusing the one-slot kD_Str_10 (same stride) would
// have leaked every entry's `text`.
void DestroyStringPair10Range(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_180005570(ignoredThis, base, count);
}

static void ExtArrayElemAssign(RtObject::ExternalArrayElement* d,
                               const RtObject::ExternalArrayElement* s) {
    new (&d->owned) System::Filename(s->owned);                  // 18002c773  0x1800067d0
    d->field_0x08 = s->field_0x08;           // 18002c779: one qword in retail (H7)
    d->field_0x0c = s->field_0x0c;
}
void AssignExtArrayElem(uint8_t* d, const uint8_t* s) {
    AssignAs<RtObject::ExternalArrayElement, &ExtArrayElemAssign>(d, s);
}

// The range destroy 0x180006ac0 calls on BOTH arms. PORTED AND REGISTERED in
// array_destroy.cpp in this same commit as `RtColl_DestroyStrings_180006ac0`
// under `kD_Str_10` - the spec row that already serves 0x180005930 and
// 0x180026b50, because 0x180006ac0 is instruction-for-instruction the same body
// (the comparison is tabulated at that B_ENTRY line, not asserted here).
//
// So this ResolveAddr is registry-first and lands on our body with the DLL
// present or absent. It does NOT open a new door onto an unported callee, which
// is the failure mode reference_porting_can_grow_the_cutover_surface.md names
// and which attempt 29 hit one level up.
void DestroyExtArrayElemRange(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_180006ac0(ignoredThis, base, count);
}

static void StringAssign(char** d, char* const* s) { AdoptString(*d, *s); }
void AssignStringRef(uint8_t* d, const uint8_t* s) { AssignAs<char*, &StringAssign>(d, s); }

// The destroy-range 0x180034860 calls on both its realloc arm (0x18003492e) and
// its SHRINK arm (0x18003497d) — the second is why this element type needs a
// `destroy` at all, since kSpec_Fill8's instantiations have no shrink branch.
//
// *** IT IS ALREADY PORTED, AND THIS LANE'S FIRST DRAFT DID NOT KNOW THAT. ***
// 0x180011010 is `kD_Str_08` in src/engine/collection/array_destroy.cpp — one
// of the eight helpers that file ports but deliberately leaves UNREGISTERED,
// with the note "Register it the day a caller appears". This lane is that
// caller (here, and again from 0x180029a80 whenever `TextElement::
// SetParameterCount` lands), so the B_ENTRY line and the registration were
// added there rather than a second body being written here.
//
// The first draft of this file DID write that second body, and also registered
// 0x180010ab0 — which array_destroy.cpp already registers as a J2 entry point.
// That is a SHADOWED ADDRESS: two registrations of the same address, one of them
// dead code, the failure `tools/dispatch_slot_map.py --check` reports for the
// exe side and nothing reports for this one. It surfaced only because a diff run
// printed `Collection::Array::~Array(+elem) @0x180010ab0` — a site name this
// lane had not written. **Before porting a Collection helper, grep the address:
// array.cpp and array_destroy.cpp are two files and one namespace.**
//
// Called by ADDRESS for the same reason DestroyStringExt above is: the
// registered body is in another translation unit and the registry is the door
// every other caller uses.
void DestroyStringRange(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_180011010(ignoredThis, base, count);
}

// ---------------------------------------------------------------------------
// 2. Array<GraphSeries>, 0x1800417d0  (stride 0x18)
// ---------------------------------------------------------------------------
// The stride is `LEA RAX,[RAX+RAX*2]; SHL RCX,3` = capacity * 24, align 8. A
// GraphSeries is {uint32 color; uint32 hole; Collection::Array<int32> points},
// with the nested Array at +0x08 — the same +0x08 `GraphElement::AddGraphPoint`
// (widgets_layout.cpp) already appends into, which is independent confirmation
// of the layout from a body this lane did not read.
//
// The assign, read at 0x180041890..0x1800418b1:
//     MOV EAX,[RDX] / MOV [RCX],EAX     ; the colour dword, +0x00..+0x03
//     ADD RCX,8
//     MOV [RCX],R8                      ; +0x08 = 0   (R8 is zero here)
//     MOV [RCX+8],0                     ; +0x10 = 0
//     ADD RDX,8
//     CALL 0x18001cdf0                  ; Array<int32>::operator=(dst+8, src+8)
//
// **+0x04..+0x07 IS NEVER WRITTEN BY EITHER ARM.** The colour is one dword, not
// a qword, and the nested Array starts at +0x08. That hole is declared in the
// spec's assign window below, because an oracle that compares bytes neither
// side writes fails 100% of calls and reads like a catastrophic port defect
// (reference_oracle_must_not_compare_unwritten_bytes.md, four such gaps in
// Wave 6).
//
// The destination's nested Array header is ZEROED BEFORE the operator= runs,
// and that is load-bearing rather than tidiness: on the realloc arm the
// destination is raw MemAlloc memory, and `operator=` starts by destroying the
// destination (it calls ~Array first). Handing it garbage would free a garbage
// pointer.
// `Collection::Array<int32_t>::operator=`, retail 0x18001cdf0. Unexported, so
// it is registered by ADDRESS below. It is the assign arm of the GraphSeries
// spec and has no other caller in our tree.
//
// Retail, 0x18001cdf0..0x18001ce7b:
//     if (dst == src) return dst;                 // CMP RCX,RDX / JZ
//     ~Array(dst);                                // CALL 0x18001c750  (OURS)
//     n   = src->_size;
//     old = dst->_size;                           // ZERO — the dtor just ran
//     dst->_size = n;
//     if (n > dst->_capacity) {
//         dst->_capacity = n;
//         if (n == old + 1 && old != 0) dst->_capacity = n * 3;
//         dst->_data = MemAlloc((int64)dst->_capacity * 4, 4);
//     }
//     for (i = 0; i < dst->_size; ++i) {          // bound RE-READ every pass
//         p = dst->_data + i*4;                   // base RE-READ every pass
//         if (p == 0) continue;
//         *p = *(int32_t*)(src->_data + i*4);     // src base RE-READ too
//     }
//     return dst;                                 // RAX = dst
//
// THE `old` TREBLE BRANCH IS DEAD IN RETAIL AND IS KEPT ANYWAY. `old` is read
// AFTER the destructor has zeroed `_size`, so it is always 0 and the `* 3`
// growth arm can never be taken here. Deleting it would be a correct-looking
// simplification of a body whose whole value is being the same instructions —
// and this project has been burned by exactly that shape
// (docs/history/runtime_port/WAVE9.md's `dirAlias` finding: a local that looks dead and
// is not). It is transcribed, with this note, rather than folded away.
//
// The `~Array` goes through ResolveAddr rather than calling ArrayDtorImpl
// directly, so the 0x18001c750 site's own `calls=` counts it and so the door is
// the same one every other by-address caller of that address uses.
void* RtColl_I32ArrayAssign(Collection::Array<int32_t>* dst,
                            const Collection::Array<int32_t>* src) {
    if (dst == src) return dst;

    RtColl_ArrayDtor_18001c750(dst);

    const int32_t n   = src->_size;
    const int32_t old = dst->_size;          // always 0 here — see above
    dst->_size = n;
    if (n > dst->_capacity) {
        dst->_capacity = n;
        if (n == static_cast<int32_t>(old + 1) && old != 0)
            dst->_capacity = static_cast<int32_t>(n * 3);
        dst->_data = static_cast<int32_t*>(
            MemAlloc(static_cast<uint64_t>(static_cast<int64_t>(dst->_capacity)) * 4, 4));
    }
    for (int32_t i = 0; i < dst->_size; ++i) {
        int32_t* p = dst->_data + i;
        if (p == nullptr) continue;
        *p = src->_data[i];
    }
    return dst;
}

static void GraphSeriesAssign(UI::GraphSeries* d, const UI::GraphSeries* s) {
    d->colour = s->colour;
    /* +0x04 .. +0x07 deliberately untouched — retail does not write them */
    d->points._data     = nullptr;          // +0x08 = 0
    d->points._size     = 0;                // +0x10 = 0 (one qword in retail, H7)
    d->points._capacity = 0;
    // BY ADDRESS, not as a direct C++ call to the body two functions up — the
    // same convention `Assign38Call` and `DestroyStringExt` above already
    // follow for an element operation that is a CALL in retail. It is not
    // pedantry: the FIRST version of this line called the inner body directly,
    // which is the same instructions but skips the registered entry point, so
    // `0x18001cdf0`'s diff site never fired and the run reported no coverage
    // for a whole ported body. Retail's 0x1800418b1 is a CALL to that address;
    // going through the registry is both faithful and instrumented.
    RtColl_I32ArrayAssign_18001cdf0(&d->points, &s->points);
}
void AssignGraphSeries(uint8_t* d, const uint8_t* s) {
    AssignAs<UI::GraphSeries, &GraphSeriesAssign>(d, s);
}

// 0x1800419b0, the destroy-range for the same element — and, exactly like
// 0x180011010 above, ALREADY PORTED in array_destroy.cpp as `kD_InnerArr_18`
// ("Array @+0x008, stride 0x018"), and left unregistered there for want of a
// caller. This lane supplies the caller; the A_ENTRY line and the registration
// were added in that file. Retail iterates the records at stride 0x18 and, for
// the nested Array at record+8, runs the same free-and-zero `~Array` runs.
//
// Reached by address so the registry is the door, as above.
void DestroyGraphSeriesRange(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyInner_1800419b0(ignoredThis, base, count);
}

// ============================================================================
// The per-address specs.
//
// Every row was read out of that address's own disassembly. `stride` is the
// SHL/IMUL at the MemAlloc call, `align` is the immediate in EDX, and the
// element ops are the loop body.
// ============================================================================

const ArraySpec kSpec_Bool = {                    // 0x180008b90  Array<bool>
    "Array<bool>", 1, 1, &Assign1,
    nullptr,   // NEW TAIL LEFT UNINITIALISED — retail's else arm is empty
    nullptr, true, 0, 0, 0, 0, 0, 0 };

const ArraySpec kSpec_I32 = {                     // 0x18002fea0  Array<int32>
    "Array<int32>", 4, 4, &Assign4,
    nullptr,   // NEW TAIL LEFT UNINITIALISED
    nullptr, true, 0, 0, 0, 0, 0, 0 };

const ArraySpec kSpec_U64Zero = {                 // 0x180008ff0  Array<Time>
    "Array<Time>", 8, 8, &Assign8, &InitZero8, nullptr, true, 0, 0, 0, 0, 0, 0 };

// WAVE 9 LANE PKG2 rung 4b-ii — 0x180024d70, Array<CompressedStream::Header>.
// A NEW SPEC, and the reason is one field. Byte-similarity puts it closest to
// kSpec_U64Zero (first difference at 88 of 352), and U64Zero agrees on element
// size 8, on Assign8 and on InitZero8 — it disagrees ONLY on the ALIGNMENT
// handed to MemAlloc: 0x180008ff0 is `MOV EDX,0x8`, 0x180024d70 is
// `MOV EDX,0x4`, checked in the disassembly of both. Reusing U64Zero because it
// "looks like" this one would have passed a wrong alignment to the allocator on
// every packfile block directory. Similarity is corroboration, not evidence.
const ArraySpec kSpec_U64ZeroA4 = {               // 0x180024d70  Array<Header>
    "Array<CompressedStream::Header>", 8, 4, &Assign8, &InitZero8, nullptr, true,
    0, 0, 0, 0, 0, 0 };

// 0x180024c10 — Array<CompressedStream::Window>, the window table
// CompressedStream::SetWindowCount grows. Element stride 0x18 (`LEA
// RCX,[RAX+RAX*2]; SHL RCX,3`), align 8 (`MOV EDX,0x8`), both read from the
// body. Distinct from every spec above on BOTH the element size and the tail
// init, so unlike kSpec_U64ZeroA4 there was no lookalike to be tempted by.
// WAVE 9 LANE PKG2 rung 4c-ii - 0x1800298f0, the packfile ENTRY array.
// Element is 16 bytes: a System::Filename at +0x00, then two int32 at +0x08
// (offset) and +0x0c (size) -- the PackageFileEntry already in
// src/engine/io/FileSystem.h, whose stride and field offsets this body
// independently confirms (SHL RCX,0x4, MOV EDX,0x8, and the two dword copies at
// 1800299c3/1800299c9).
//
// *** THIS ALMOST REUSED kSpec_StringExt, AND THAT WOULD HAVE BEEN A DEFECT. ***
// AssignStringExt is byte-for-byte what this element needs for its first eight
// bytes -- null the slot, adopt, increment the refcount -- and then copies the
// remaining eight verbatim, which is exactly the two dwords. Every visible
// property matches. But the element assign here is a CALL to 0x1800067d0,
// ??0Filename@System@@QEAA@AEBV01@@Z, and that function does one thing
// AssignStringExt does not: it ends with TWO calls to
// Filename::AdjustDelimiters (0x18000682a and 0x180006832). Reusing the
// lookalike would have left every packfile entry name with its separators
// unnormalised -- forward slashes never rewritten to backslashes -- which
// breaks lookup for any entry that uses them and breaks NOTHING otherwise, so
// it would have passed a green run.
//
// Third time this family has offered a wrong answer by resemblance, and the
// first where the difference was not an immediate but a TRAILING CALL. The
// assign below therefore delegates to the ported copy ctor rather than
// reimplementing its String half.
static void PkgEntryAssign(RtIO::PackageFileEntry* d, const RtIO::PackageFileEntry* s) {
    new (&d->name) System::Filename(s->name);   // 1800299bd  the copy ctor 0x1800067d0 into the raw element
    d->offset = s->offset;                   // 1800299c3
    d->size   = s->size;                     // 1800299c9
}
void AssignPkgEntry(uint8_t* d, const uint8_t* s) {
    AssignAs<RtIO::PackageFileEntry, &PkgEntryAssign>(d, s);
}

// The range destructor 0x1800298f0 calls on BOTH arms -- after a realloc
// (180029a0e, over the whole old array) and on a SHRINK with no realloc
// (180029a68, over the tail only).
//
// *** WAVE 9 LANE DESTROY, 2026-09-08: 0x180026b50 IS NOW PORTED, and the
// sentence that used to stand here was half right in a way that mattered. ***
// It said this was "a different function from DestroyStringExt's 0x180005930
// and DestroyStringRange's 0x180011010". That is true of the ADDRESS -- three
// distinct entry points -- and it is FALSE of the BODY: 0x180026b50 and
// 0x180005930 differ only in an alignment NOP and a duplicated store into this
// function's own SEH scope slot, neither of which any caller can observe. Both
// listings are tabulated side by side above the B_ENTRY block in
// array_destroy.cpp, which is where the claim is made from the disassembly
// rather than from resemblance, and where the probe row that tests it against
// the SHIPPED 0x180026b50 is named.
//
// So the element DOES still need its own ArraySpec -- because of the ASSIGN
// (0x1800067d0's two trailing Filename::AdjustDelimiters calls, the third
// near-miss this family produced) -- but it does NOT need its own DestroySpec.
// The two halves of "why kSpec_PkgEntry exists" have different answers and were
// being carried as one.
//
// This still forwards by ADDRESS, deliberately: ResolveAddr consults the port
// registry first, so it now reaches array_destroy.cpp's
// RtColl_DestroyStrings_180026b50 with the DLL present or absent, and the
// address stays spelled as a literal here so tools/addr_door_audit.py and
// tools/runtime_census.py can both still see the edge.
void DestroyPkgEntryRange(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_180026b50(ignoredThis, base, count);
}

// NO SEED ZEROS, and that is read out of the callee rather than copied from the
// sibling spec: 0x1800067d0's FIRST action is `MOV qword ptr [RBX],0`
// (1800067f0), and the read at 1800067f3 reads back the zero it just wrote. It
// never reads uninitialised memory, so there is nothing for a seed to protect.
const ArraySpec kSpec_PkgEntry = {                // 0x1800298f0  Array<Entry>
    "Array<IO::PackageFile::Entry>", 0x10, 8, &AssignPkgEntry, nullptr,
    &DestroyPkgEntryRange, true,
    /*assign unwritten*/ 0, 0,
    /*init   unwritten*/ 0, 0,
    /*seed zero       */ 0, 0 };

const ArraySpec kSpec_Window = {                  // 0x180024c10  Array<Window>
    "Array<CompressedStream::Window>", 0x18, 8, &Assign18, &InitWindow, nullptr,
    true, 0, 0, 0, 0, 0, 0 };

// WAVE 9 LANE GFX3, 2026-09-08 — 0x18005fb00, Array<Graphics::Scene::Node>.
// The node array `Graphics::Scene`'s grid appends to. Read out of its own body
// rather than inferred from the element type, and it lands one field away from
// kSpec_Window, which is a lookalike this lane had to rule out rather than
// adopt (fourth time this family has offered a wrong answer by resemblance;
// the earlier three differed in an alignment immediate, a shift, and a trailing
// call):
//   * stride 0x18 — `LEA RCX,[RAX+RAX*2]; SHL RCX,3` at 18005fb53/fb57. Same
//     as kSpec_Window.
//   * align 8 — `MOV EDX,0x8` at 18005fb5b. Same as kSpec_Window.
//   * assign = three plain qword moves at +0x00/+0x08/+0x10 (18005fba2..fbb4),
//     with no zero-first store. Same body as Assign18.
//   * *** init = NONE. *** This is the difference. 0x180024c10 has an `else`
//     arm that builds an empty Window; 0x18005fb00's loop has NO else arm at
//     all — `CMP R8,RSI; CMOVL RCX,R10` picks the old element or the FILL and
//     there is no third case, because every caller of this address passes a
//     fill. Reusing kSpec_Window would have run InitWindow over the new tail on
//     any two-argument call, writing three qwords retail never writes.
// So it is a distinct row, and it is the three-argument form only.
//
// The element's +0x14 is a DON'T-CARE the caller supplies as an uninitialised
// stack dword (see src/engine/graphics/scene_grid.cpp's GridAdd banner and
// src/engine/graphics/Scene.h). The assign COPIES it, faithfully; nothing
// here has to know that, and no oracle may compare it.
const ArraySpec kSpec_Node18 = {                  // 0x18005fb00  Array<Scene::Node>
    "Array<Graphics::Scene::Node>", 0x18, 8, &Assign18,
    nullptr,   // NO else arm in retail's loop — the three-argument form only
    nullptr, true, 0, 0, 0, 0, 0, 0 };

const ArraySpec kSpec_PtrZero = {                 // 0x180008d60, 0x1800764c0
    "Array<InternalPtr>", 8, 8, &Assign8Z, &InitZero8, nullptr, true, 0, 0, 0, 0, 0, 0 };

// WAVE 25 LANE INTERNAL42 — 0x18002a810, the Persistence worker's block
// directory ({id, offset, size}, three dwords). A NEW ROW, and the reason is
// the same one kSpec_U64ZeroA4 was written for: it is close to several existing
// rows and identical to none. Read out of the body, not out of the caller:
//   stride 0xc   LEA RCX,[RAX+RAX*2] ; SHL RCX,0x2   at 0x18002a863
//   align  4     MOV EDX,0x4                         at 0x18002a86b
//   assign       three dword moves                   at 0x18002a89d
//   init         NONE — the else arm of the copy loop is empty, so the new
//                tail is left UNINITIALISED (`JGE 0x18002a8b3` past it)
//   destroy      NONE — retail frees the old buffer and destroys nothing
const ArraySpec kSpec_Blk12 = {                   // 0x18002a810
    "Array<{id,offset,size}>", 0xc, 4, &Assign12,
    nullptr,   // NEW TAIL LEFT UNINITIALISED
    nullptr, true, 0, 0, 0, 0, 0, 0 };

const ArraySpec kSpec_Transform = {               // 0x180059140  Array<Transform>
    "Array<Transform>", 0x1c, 4, &Assign1c, &InitIdentityTransform, nullptr, true, 0, 0, 0, 0, 0, 0 };

const ArraySpec kSpec_I32Neg1 = {                 // 0x18007b960  Array<int>, fill -1
    "Array<int32>(-1)", 4, 4, &Assign4, &InitNeg1, nullptr, true, 0, 0, 0, 0, 0, 0 };

const ArraySpec kSpec_Fill8 = {                   // 0x18001f990  Array<T*>
    "Array<ptr>", 8, 8, &Assign8, nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };

// BATCH 4 — 0x18000c300, `Array<T>::SetSize(4)` for a 0x8ba0-byte element, the
// one-argument constant-folded shape. Read off its own body:
//   IMUL / `(longlong)capacity * 0x8ba0` at the MemAlloc  -> stride 0x8ba0
//   MOV EDX,0x4                                           -> align 4
//   the element copy is `memcpy(dst, src, 0x8ba0)` (retail hand-unrolls a
//   16-byte SSE loop when both pointers are 16-aligned and calls `memcpy`
//   otherwise — same bytes either way, so one memcpy is faithful)
//   the copy loop's `else` arm does not exist: elements in [oldSize, 4) are
//   LEFT UNINITIALISED, so `init` stays null.
// The element size is the only thing about the type this lane can state: the
// sole caller is Engine::Play (0x1800a0d10) and nothing there names it.
void AssignBlob8ba0(uint8_t* d, const uint8_t* s) { std::memcpy(d, s, 0x8ba0); }
const ArraySpec kSpec_Blob8ba0 = {                // 0x18000c300
    "Array<blob0x8ba0>", 0x8ba0, 4, &AssignBlob8ba0, nullptr, nullptr, true,
    0, 0, 0, 0, 0, 0 };

const ArraySpec kSpec_Fill8Z = {                  // 0x180076900, 0x18007a0d0
    "Array<InternalPtr>(fill)", 8, 8, &Assign8Z, nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };

const ArraySpec kSpec_Fill4 = {                   // 0x180023830  Array<uint32>
    "Array<uint32>", 4, 4, &Assign4, nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };

const ArraySpec kSpec_Fill16 = {                  // 0x18004f790, 0x18004f8e0
    "Array<callback16>", 0x10, 8, &Assign16, nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };

// 0x180051aa0 — Graphics::ParticleSystem::AddEmitter's emitter-slot array.
// Same 16-byte element and Assign16 as kSpec_Fill16, but the body's own
// `MemAlloc(n << 4, 4)` passes align=4, not 8 (read from the body, not
// assumed from the sibling row above) — a distinct spec so the allocator call
// stays byte-faithful.
const ArraySpec kSpec_Fill16A4 = {                // 0x180051aa0
    "Array<EmitterSlot16>", 0x10, 4, &Assign16, nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };

// WAVE 9 LANE UIF — Array<UI::SpriteDraw>, the sprite batch behind
// SpriteContext::AddSpriteDraw (append, 0x180078520) and
// SpriteContext::FlushDeferred (clear, 0x1800783d0). stride 0x28, align 8
// (`MOV EDX,0x8` at 0x18007857b and 0x18007841b in the two bodies).
//
// ASSIGN arm covers the whole element (five qwords) -> no unwritten window.
// INIT arm leaves TWO holes, +0x1c..+0x1f and +0x25..+0x27 (see
// InitSpriteDraw). `ArraySpec` carries ONE window per arm, so only the first
// is expressible here. That is NOT a live oracle risk for either registration
// below and this is why, stated rather than assumed: the init arm runs only
// for elements in [oldSize, newSize) when the caller passed NO fill, and
//   * 0x180078520 is the three-argument form — every grown element takes the
//     ASSIGN arm, so the init arm is unreachable through it;
//   * 0x1800783d0 has newSize folded to the constant 0, so it never grows at
//     all and the init arm is unreachable through it too.
// The +0x25..+0x27 hole is recorded here for whoever registers a two-argument
// Array<SpriteDraw>::SetSize, which would be the first caller that can reach
// it (reference_never_written_needs_by_whom.md: the extent belongs to the ARM,
// not to the type). The same one-window limit is already filed against
// kSpec_Sprite38 above.
const ArraySpec kSpec_SpriteDraw = {              // 0x180078520, 0x1800783d0
    "Array<UI::SpriteDraw>", 0x28, 8, &Assign28, &InitSpriteDraw, nullptr, true,
    0, 0, 0x1c, 0x20, 0, 0 };

// --- the non-trivial three --------------------------------------------------
// *** OPEN, filed by Wave 9 lane D2 2026-09-07: this row's unwritten window is
// WRONG, and it is wrong in the direction that fails 100% of comparisons. ***
// Lane D2 disassembled 0x180004220 (the `Assign38Call` target) and found it
// writes +0x00..+0x33 and stops: the element is 0x38 bytes, so **+0x34..+0x37
// is never written by the ASSIGN**. This row says `unwrittenLo = unwrittenHi =
// 0`, i.e. "no such window". It has not bitten because this lane's probe seeds
// all-zero elements, so those four bytes agree by accident.
//
// NOT changed by lane D2, because `unwritten*` is documented in
// array_internal.h as the window retail's DEFAULT (init) arm leaves alone and
// this window is on the ASSIGN arm — widening that meaning from another lane,
// with no way to run this probe, is how an oracle quietly stops comparing bytes
// it should compare. It is lane Q's call whether to widen the field or add a
// second window. See docs/history/runtime_port/WAVE9_LANE_D2.md.
// ASSIGN-ARM WINDOW +0x34..+0x37, read out of 0x180004220 for this correction:
// it refcount-assigns the two String pointers at +0x00 and +0x08, copies NINE
// dwords at +0x10,+0x14,+0x18,+0x1c,+0x20,+0x24,+0x28,+0x2c,+0x30 -- and STOPS.
// The element is 0x38 bytes, so +0x34..+0x37 is never written. That is exactly
// src/engine/ui/SpriteSheet.h's `_pad34`: retail's own copy-assign does not
// copy the padding, which independently confirms the field IS padding.
//
// This spec has NO init arm (it is the three-argument form, so the new tail
// goes through `assign` with `fill` too), which is why the init window is 0/0
// and why the assign window must be excluded for EVERY element the loop
// touched, not just the tail. Found by lane D2 while porting 0x1800059e0 and
// filed as latent because the probe was seeding zeros -- see WAVE9_LANE_Q.md
// 3.4b.
//
// SEED ZEROS +0x00..+0x0f: both String slots are dereferenced (`INC [RAX-4]`
// in the assign, a refcount release in 0x1800059e0). Everything from +0x10 on
// is plain scalars and IS seeded non-zero, which is what makes the +0x34 window
// testable at all.
// UI leftovers batch -- the CaptureEntry pair. Both share AssignCaptureEntry
// and Destroy28Call; they differ only in the 2-arg form's default-tail arm
// (see InitCaptureEntryTail's banner). NON-TRIVIAL (owned String reference,
// per-element destroy), same tier as kSpec_Sprite38 above and registered
// unconditionally for the same reason 0x180078950 is: nothing in either spec
// calls an unexported helper still on the DLL -- both AssignCaptureEntry and
// Destroy28Call are ours.
const ArraySpec kSpec_CaptureEntry = {            // 0x18004bde0 (3-arg, with fill)
    "Array<UI::SpriteCapture::CaptureEntry>", 0x28, 8, &AssignCaptureEntry,
    nullptr, &Destroy28Call, false,
    /*assign unwritten*/ 0x04, 0x08,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0x08, 0x10 };

const ArraySpec kSpec_CaptureEntryZ = {           // 0x18004ba90 (2-arg, default tail)
    "Array<UI::SpriteCapture::CaptureEntry> (2-arg)", 0x28, 8, &AssignCaptureEntry,
    &InitCaptureEntryTail, &Destroy28Call, false,
    /*assign unwritten*/ 0x04, 0x08,
    /*init   unwritten*/ 0x00, 0x08,
    /*seed zero       */ 0x08, 0x10 };

// UI leftovers batch -- UI::UserInterface::_events, three-argument form only
// (0x180033690's only caller passes a fill; there is no two-argument sibling
// in this worklist). TRIVIAL: no owned resource, no destroy -- same
// classification kSpec_PtrZero gets for its own zero-then-copy idiom.
const ArraySpec kSpec_Event30 = {                 // 0x180033690
    "Array<UI::UserInterface::Event>", 0x30, 8, &AssignEvent30, nullptr, nullptr, true,
    /*assign unwritten*/ 0x24, 0x28,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0,    0 };

// UI leftovers batch -- UI::Sprite, the two-argument form (0x1800370c0,
// `SpriteContext::Begin` clearing SpriteCapture's own `_sprites`/`_entries`
// pair to size 0). TRIVIAL: Assign16 is the existing pure byte copy: no
// refcount touch anywhere in this element, so no destroy is needed either.
const ArraySpec kSpec_SpriteIndexZ = {            // 0x1800370c0
    "Array<UI::Sprite> (2-arg, index=-1 tail)", 0x10, 8, &Assign16,
    &InitSpriteIndexNeg1, nullptr, true,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0x0c, 0x10,
    /*seed zero       */ 0,    0 };

const ArraySpec kSpec_Sprite38 = {                // 0x180004e30
    "Array<SpriteEntry:0x38>", 0x38, 8, &Assign38Call, nullptr, &Destroy38, false,
    /*assign unwritten*/ 0x34, 0x38,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0x00, 0x10 };

// --- WAVE 26 LANE INT33 -----------------------------------------------------
// 0x180078950 — `Array<UI::SpriteSheet::SpriteInfo>::SetSize(this, int)`, the
// TWO-argument sibling of 0x180004e30 above. `ui/spritesheet_vtable.cpp:397`
// filed it as "a one-row follow-on for the collection family"; this is that row.
//
// *** IT DOES NOT TAKE kSpec_Sprite38, AND THAT IS THE WHOLE POINT OF THE ROW.
// *** The two bodies share the element type, the stride, the alignment, the
// assign helper AND the destroy helper — everything a reader would use to decide
// they are the same spec — and they differ in the one field that is reachable
// only through the two-argument form. `kSpec_Sprite38` has `init = nullptr`,
// which array_internal.h defines as "retail leaves the new tail UNTOUCHED", and
// that is CORRECT FOR 0x180004e30 because it is the three-argument form: every
// grown element takes the assign arm with the caller's `fill`, so its init arm
// is unreachable and was never written down. 0x180078950 has NO fill, so its
// init arm is the ONLY path a grown element takes — and retail zeroes 0x34
// bytes there. Reusing kSpec_Sprite38 would have left the new tail holding the
// previous allocation's bytes, including two String POINTERS that the next
// destroy would then release. Silently, with a clean build and a green census.
//
// The init arm, transcribed from 0x180078a0e..0x180078a38 — six qword zero
// stores at +0x00,+0x08,+0x10,+0x18,+0x20,+0x28 and ONE DWORD at +0x30:
//
//   180078a0e  MOV [RCX],R8        (R8 = 0)      +0x00
//   180078a11  MOV [RCX+0x08],R8                 +0x08
//   180078a15  MOV qword [RCX+0x10],0            +0x10
//   180078a1d  MOV qword [RCX+0x18],0            +0x18
//   180078a25  MOV qword [RCX+0x20],0            +0x20
//   180078a2d  MOV qword [RCX+0x28],0            +0x28
//   180078a35  MOV dword [RCX+0x30],R8D          +0x30   <- DWORD, not qword
//
// so it covers +0x00..+0x33 and **+0x34..+0x37 is never written**, the same
// four bytes `SpriteSheet.h` names `_pad34` and the same window the ASSIGN arm
// already has recorded against it. Two arms, two independent readings, one
// window — which is why both are spelled out below rather than one being
// inferred from the other.
//
// Everything else read from the body rather than inherited:
//   stride 0x38   IMUL RCX,RCX,0x38   at 0x1800789ae
//   align  8      MOV EDX,8           at 0x1800789b2, before CALL 0x1800c7a50
//   assign        CALL 0x180004220    at 0x1800789f7   (== Assign38Call)
//   destroy       CALL 0x1800059e0    at 0x180078a54 and 0x180078aab
//                 (the second is the SHRINK path, over the truncated tail)
const ArraySpec kSpec_Sprite38Z = {               // 0x180078950
    "Array<SpriteEntry:0x38> (2-arg, tail ZEROED)", 0x38, 8,
    &Assign38Call, &InitSprite38Z, &Destroy38, false,
    /*assign unwritten*/ 0x34, 0x38,
    /*init   unwritten*/ 0x34, 0x38,
    /*seed zero       */ 0x00, 0x10 };

// BOTH ARMS SKIP +0x2c..+0x2f, for two independent reasons -- which is a fact,
// not an assumption. The init arm is InitEffectSlot above. The assign arm,
// 0x180051650, was disassembled for this correction: dwords at +0x00,+0x04,
// +0x08,+0x0c, qwords at +0x10,+0x18, dwords at +0x20,+0x24,+0x28 -- and then
// it jumps straight to +0x30. +0x2c is skipped there too. It finishes by
// zeroing +0x30 and +0x38 and calling 0x180050d30 on the sub-object at +0x30,
// so the tail IS fully initialised before that call and has no window of its
// own.
//
// SEED ZEROS +0x30..+0x3f: that sub-object goes to 0x180050d30 (lane D's J10,
// unread by this lane) on the copy path and to the destroy-range 0x180052110
// on the free path, and a garbage pointer there would be freed. +0x00..+0x2f is
// plain scalars and IS seeded non-zero.
const ArraySpec kSpec_EffectSlot = {              // 0x180051390
    "Array<EffectSlot:0x40>", 0x40, 8, &Assign40Call, &InitEffectSlot, &Destroy40,
    false,
    /*assign unwritten*/ 0x2c, 0x30,
    /*init   unwritten*/ 0x2c, 0x30,
    /*seed zero       */ 0x30, 0x40 };

// NO WINDOW EITHER ARM: AssignStringExt writes both qwords of the 0x10-byte
// element (+0x00 via the refcounted path, +0x08 verbatim), so the assign covers
// the whole stride, and there is no init arm. Checked against 0x180061480's
// inlined loop rather than assumed, because the two specs above both turned out
// to have one.
//
// SEED ZEROS +0x00..+0x07: the String slot is dereferenced by the assign's
// `INC [RAX-4]` and by 0x180005930's release. +0x08, the ExternalPtr, is copied
// verbatim and never dereferenced by either, so it IS seeded non-zero.
// 2026-09-12 -- Video::VertexLayout::Element, 0x180056240's SetSize (cutover
// worklist, src/engine/video/vertexlayout.cpp). Read out of the body:
//   stride 0x20  (`SHL RCX,5` on the capacity), align 8
//   assign       String adopt at +0x00 (`dst=0; if (src) { dst=src; ++[src-4] }`)
//                then five dwords +0x08..+0x1b copied; +0x1c NOT written
//   init         three qwords + one dword zeroed: +0x00..+0x1b; +0x1c NOT written
//   destroy      0x18004fe20, the String release loop at stride 0x20
//                (array_destroy.cpp kD_Str_20)
static void VertexElementAssign(Video::VertexLayout::Element* d,
                                const Video::VertexLayout::Element* s) {
    AdoptString(d->name._data, s->name._data);  // +0x00
    d->stream     = s->stream;              // +0x08..+0x1b, five dwords
    d->offset     = s->offset;
    d->usage      = s->usage;
    d->usageIndex = s->usageIndex;
    d->format     = s->format;
}
void AssignVertexElement(uint8_t* d, const uint8_t* s) {
    AssignAs<Video::VertexLayout::Element, &VertexElementAssign>(d, s);
}
static void VertexElementInit(Video::VertexLayout::Element* d) {
    d->name._data = nullptr;                // +0x00..+0x1b zeroed
    d->stream     = 0;
    d->offset     = 0;
    d->usage      = Video::VertexLayout::Usage();
    d->usageIndex = 0;
    d->format     = Video::VertexLayout::Format();
}
void InitVertexElement(uint8_t* d) { InitAs<Video::VertexLayout::Element, &VertexElementInit>(d); }
void DestroyVertexElement(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_18004fe20(ignoredThis, base, count);
}
const ArraySpec kSpec_VertexElement = {           // 0x180056240
    "Array<VertexLayout::Element>", 0x20, 8, &AssignVertexElement, &InitVertexElement,
    &DestroyVertexElement, false,
    /*assign unwritten*/ 0x1c, 0x20,
    /*init   unwritten*/ 0x1c, 0x20,
    /*seed zero       */ 0x00, 0x08 };

const ArraySpec kSpec_StringExt = {               // 0x180061480
    "Array<{String*,ExternalPtr}>", 0x10, 8, &AssignStringExt, nullptr,
    &DestroyStringExt, true,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0x00, 0x08 };

// --- WAVE 9 LANE UIW's two ------------------------------------------------
//
// NO WINDOW EITHER ARM. AssignStringRef's unconditional `dst = 0` before the
// null test means both arms write all 8 bytes; there is no init arm.
// SEED ZEROS the whole stride: the assign dereferences the source pointer
// (`INC [RAX-4]`) and the destroy dereferences and frees the destination's.
const ArraySpec kSpec_StringRef = {               // 0x180034860
    "Array<System::String>", 8, 8, &AssignStringRef, nullptr,
    &DestroyStringRange, false,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0x00, 0x08 };

// --- Cutover closure batch 2: Graphics::Material::_parameters, 0x1800616c0 --
//
// Two-argument `SetSize(this, int)`, stride 0x18. Read out of 0x1800616c0's
// own body:
//   ASSIGN (element in range): zero +0x00, then AssignStringRef's zero-then-
//     addref-copy of the String* at +0x00; +0x08..+0x17 copied verbatim as
//     four dwords (one memcpy, no refcounted field in that half).
//   INIT (grown past old size): all three qwords (+0x00..+0x17) zeroed.
//   DESTROY: 0x180005880 (Collection_Array_MaterialParam_DestroyStrings, this
//     file's `kD_Str_18at00` in array_destroy.cpp), called on both the
//     realloc arm and the shrink-tail arm — read from the disassembly, not
//     assumed from kSpec_StringRef's shape.
// MemAlloc's align argument is 8 (`MOV EDX,8` at 0x1800617d9).
static void MaterialConstantAssign(RtGraphics::MaterialConstant* d,
                                   const RtGraphics::MaterialConstant* s) {
    StringAssign(&d->name._data, &s->name._data);         // +0x00: String* ref
    std::memcpy(d->value, s->value, sizeof d->value);     // +0x08..+0x17: raw copy
}
void AssignStringPlus16(uint8_t* d, const uint8_t* s) {
    AssignAs<RtGraphics::MaterialConstant, &MaterialConstantAssign>(d, s);
}
void InitZero18(uint8_t* d) {
    std::memset(d, 0, 0x18);
}
// Reached by ADDRESS, not a direct call into array_destroy.cpp's translation
// unit — same door DestroyStringRange/DestroyVertexElement already use, so a
// caller elsewhere in the tree that registers 0x180005880 differently is
// picked up here too.
void DestroyMaterialParamStrings(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_180005880(ignoredThis, base, count);
}
const ArraySpec kSpec_MaterialParam = {           // 0x1800616c0
    "Array<Material::Parameter>", 0x18, 8, &AssignStringPlus16, &InitZero18,
    &DestroyMaterialParamStrings, false,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0x00, 0x08 };

// --- WAVE 10 LANE UIORACLE -------------------------------------------------
//
// 0x180029a80, the TWO-ARGUMENT sibling of 0x180034860 -- the one
// array_destroy.cpp's Wave 9 note predicted would arrive: "It will get a
// SECOND caller the day 0x180029a80 (the two-argument sibling,
// `TextElement::SetParameterCount`'s) lands." That day is this commit.
//
// IT IS *NOT* kSpec_StringRef, AND THE DIFFERENCE IS THE ONE FIELD THE
// TWO-ARGUMENT FORM EVEN HAS. kSpec_StringRef's `init` is null, which this
// file's ArraySpec comment defines as "retail leaves the new tail untouched"
// -- true of 0x180034860, whose only caller passes a fill so the tail always
// takes the ASSIGN arm and there is no else arm to describe. 0x180029a80 has
// one, and it is not empty:
//
//     180029b2a  MOV qword ptr [RDX],R10      ; R10 == 0 -- the `else` arm
//
// so the tail is ZEROED. Reusing kSpec_StringRef here -- which was the first
// draft, on the "same element type, same destroy, same assign" argument --
// would have left every newly grown System::String slot holding whatever the
// fresh MemAlloc block contained, and the next SetParameterCount's destroy arm
// would have decremented a refcount through that garbage. Similarity is
// corroboration, not evidence (kSpec_U64ZeroA4's note, same trap, different
// field).
//
// Everything else is read from the same disassembly and agrees with
// kSpec_StringRef: stride 8 (`SHL RCX,0x3` at 180029ad3), align 8 (`MOV EDX,0x8`
// at 180029ad7), assign = zero-then-copy-with-addref (180029b11..180029b20),
// destroy = 0x180011010 (kD_Str_08, already ported and registered in
// array_destroy.cpp) on BOTH the realloc arm (180029b40) and the shrink arm
// (180029b8f).
// --- WAVE 26 LANE INT33 -----------------------------------------------------
// 0x180075d20 — `Array<UI::DialogNamedTemplate>::SetSize(this, int)`, the
// two-argument form. Element `{ qword copied verbatim at +0x00, System::String*
// at +0x08 }`, stride 0x10.
//
// NO WINDOW ON EITHER ARM, and both halves of that are measured, not one
// inferred from the other:
//   * ASSIGN covers +0x00..+0x0f because the store at +0x08 is an UNCONDITIONAL
//     zero BEFORE the null test (0x180075dcb), so the byte range is written
//     even when the source String is null.
//   * INIT covers +0x00..+0x0f -- two qword zero stores, 0x180075dee/0x180075df1.
//
// SEED ZEROS +0x08..+0x10 and NOT +0x00..+0x08. Only the String slot is
// dereferenced (`INC [RAX-0x4]` in the assign, a refcount release in
// 0x180039640); +0x00 is copied verbatim by the assign and never touched by the
// destroy, so it IS seeded non-zero -- which is what makes a mirrored
// AssignStringExt detectable instead of agreeing by accident on zeros.
//
//   stride 0x10   SHL RCX,0x4         at 0x180075d71
//   align  8      MOV EDX,8           at 0x180075d75
//   assign        inline              at 0x180075dc4  (AssignExtStringAt08)
//   init          inline              at 0x180075dee  (InitZero10)
//   destroy       CALL 0x180039640    at 0x180075e0f and on the shrink path
const ArraySpec kSpec_ExtStr10at08 = {            // 0x180075d20
    "Array<{qword, String*@+8}>", 0x10, 8,
    &AssignExtStringAt08, &InitZero10, &DestroyExtStringAt08, false,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0x08, 0x10 };

// ============================================================================
// WAVE 27 LANE FRONTIER - 0x18002c6a0, `Array<Object::ExternalArrayElement>`
//
// THIS IS THE ADDRESS CUTOVER ATTEMPT 29 DIED ON (EXEC-of-0 at
// ArraySetSize+0x4d, from src/engine/object/external_load.cpp:157). The whole
// body was disassembled for this row; nothing below is inherited.
//
// *** THE WAVE 26 WARNING HELD, AND ITS REASONING WAS ONE STEP OFF. ***
// WAVE26_CLOSED.md section 6 item 1 said the three ported siblings in
// achievement_manager.cpp (Array<bool>, Array<Time>, Array<InternalPtr>) are
// the WRONG TEMPLATE, because this element carries a refcounted String and
// those are POD "realloc+memcpy" arrays. The CONCLUSION is right - reusing
// kSpec_Bool / kSpec_U64Zero / kSpec_PtrZero here would leak a refcount on
// every grow and skip the release on every shrink. The PREMISE is not: those
// three are not a separate realloc+memcpy template at all, they are three
// `kSpec_*` rows of the SAME SetSizeCore this row joins, and the machinery for
// a non-trivial element (an `assign` that is a CALL, and a `destroy` that runs
// on both the realloc and the shrink arm) has existed in this file since Wave 9
// and already serves seven rows. So the danger was real and the remedy was
// already written: one spec row, not a new template.
//
// Read out of 0x18002c6a0's own disassembly:
//
//   ARITY = TWO.  `MOV dword ptr [RSP+0x10],EDX` at 0x18002c6a0 homes arg2 and
//     NOTHING homes or reads R8. Registering this behind the three-argument
//     typedef and dereferencing R8 would read a register the caller never set
//     (external_load.cpp:157 calls it with two arguments, which agrees).
//   STRIDE 0x10.  `SHL RCX,0x4` at 0x18002c70a before MemAlloc, and again as
//     `ADD RDI,0x10` at 0x18002c7a8 and `ADD R12,R12 ; LEA RDX,[RDI+R12*0x8]`
//     at 0x18002c821 (the shrink-tail base = old + newSize*0x10).
//   ALIGN 8.  `MOV EDX,0x8` at 0x18002c70e.
//   GROWTH.  *** NOT capacity*=2, and WAVE26_CLOSED.md said so without saying
//     what it IS. *** 0x18002c6ed..0x18002c703:
//         capacity = newSize
//         if (newSize == oldSize + 1 && oldSize != 0) capacity = newSize * 3
//     (`LEA EAX,[R13+0x1]`, `LEA EAX,[R15+R15*0x2]`). That is byte-for-byte
//     the rule SetSizeCore already implements for all 27 siblings - the family
//     growth policy, not a per-instantiation one. This row inherits it by
//     using SetSizeCore and adds nothing.
//   ASSIGN = a CALL plus one qword (0x18002c76d..0x18002c77d), see below.
//   INIT  = two zero qwords (0x18002c79a/0x18002c79d), whole stride covered.
//   DESTROY = 0x180006ac0 on BOTH arms - realloc (0x18002c7d4, count=oldSize)
//     and shrink-tail (0x18002c82b, count=oldSize-newSize).
//
// ELEMENT: `{System::Filename @+0x00, int32 @+0x08, int32 @+0x0c}`, and the two
// halves of that were established by two writers that never saw each other -
// external_load.cpp's dependency replay (which serializes a String then TWO
// int32s at +0x08 and +0x0c) and this body (whose assign copies +0x08..+0x0f as
// ONE qword). Both are true: the ARRAY does not care where the split is, and
// the SERIALIZER does. src/engine/object/External.h carries the struct.
const ArraySpec kSpec_ExtArrayElem = {            // 0x18002c6a0
    "Array<Object::ExternalArrayElement>", 0x10, 8,
    &AssignExtArrayElem, &InitZero10, &DestroyExtArrayElemRange, false,
    /*assign unwritten*/ 0,    0,      // CALL writes +0x00..+0x07, qword +0x08
    /*init   unwritten*/ 0,    0,      // two qword zero stores cover 0x10
    /*seed zero       */ 0x00, 0x08 }; // +0x00 is DEREFERENCED by both helpers

// WAVE 27 LANE FRONTIER - 0x180078c90, `Array<UI::StringTable::TextEntry>`.
// TWO-ARGUMENT (no R8 is homed or read; `MOVSXD RDI,EDX` at 0x180078cac is the
// only parameter past `this`). Stride 0x10 (`SHL RCX,0x4` at 0x180078ce1,
// `ADD RCX,0x10` at 0x180078d6e), align 8 (`MOV EDX,0x8` at 0x180078ce5), and
// the same family growth rule (`LEA EAX,[RSI+0x1]` / `LEA EAX,[RDI+RDI*0x2]`
// at 0x180078ccc..0x180078cd7).
//
// INIT arm = two zero qwords at 0x180078d64/0x180078d67, so InitZero10 is
// exactly it and the whole stride is covered.
//
// *** IT IS NOT kSpec_ExtStr10at08 AND IT IS NOT kSpec_ExtArrayElem. *** All
// three are stride 0x10, align 8, InitZero10, non-trivial, with a destroy on
// both arms - every property a reader would use to decide they are the same
// row. They differ in WHICH SLOTS carry a String: +0x08 only, +0x00 only, and
// BOTH. Each wrong pairing leaks or double-releases exactly one refcount.
const ArraySpec kSpec_StringPair10 = {            // 0x180078c90
    "Array<UI::StringTable::TextEntry>", 0x10, 8,
    &AssignStringPair10, &InitZero10, &DestroyStringPair10Range, false,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0x00, 0x10 }; // BOTH slots are dereferenced

// ---- VTABLE BATCH 2 (2026-09-12): four rows for the UI::*Description second
// ---- half. Re-applied by hand after merging main (this file conflicted whole,
// ---- so every anchor below was re-checked against main's text).

// 0x18004cdf0 - UI::TabDescription's tab array: each element is a PAIR of
// ExternalPtr<ElementDescription> (?Serialize@TabDescription@UI@@ runs
// 0x180038f90 over elem+0x00 and elem+0x08 in one loop, 0x1800da881/0x1800da88e)
// and ~TabDescription's array dtor (0x18004cc90) frees the buffer WITHOUT
// visiting the elements. So TRIVIAL, and no `destroy`.
//
// IT IS *NOT* kSpec_StringPair10, which has the same stride, the same align and
// the same zeroed tail. That row's two slots are refcounted Strings and it
// carries AssignStringPair10 and DestroyStringPair10Range; reusing it here would
// take a reference on two ExternalPtrs on every grow and release them on every
// shrink, against a class whose own destructor takes none. The three numbers
// were read from 0x18004cdf0's own body (`<< 4` at the MemAlloc -> stride 0x10,
// `MOV EDX,0x8` -> align 8, the else arm writing two zero qwords -> InitZero10).
const ArraySpec kSpec_ExtPtrPair10 = {            // 0x18004cdf0
    "Array<UI::TabDescription::Tab>", 0x10, 8, &Assign16, &InitZero10, nullptr, true,
    0, 0, 0, 0, 0, 0 };

// 0x1800799a0 - UI::TableDescription's cell array: {int32 col, int32 row,
// ExternalPtr<ElementDescription>} (kScalarInt over +0x00 and +0x04 and
// 0x180038f90 over +0x08, 0x18011e67f/0x18011e694/0x18011e6a2).
// ~TableDescription's array dtor (0x180079870) does not visit the elements.
//
// IT IS *NOT* kSpec_ExtPtrPair10 above, and the difference is the one thing a
// spec even has beyond stride and align: the DEFAULT TAIL. That row's else arm
// writes two ZERO qwords; this one writes `0xffffffffffffffff` then zero, i.e.
// a new cell starts at col = -1, row = -1 - exactly the "wildcard" value
// TableDescription::CreateInstance's fallback search tests for. A zeroed tail
// would make every grown cell claim column 0, row 0.
static void TableCellInit(UI::TableDescription::Cell* d) {
    d->column = -1;                         // ONE qword of -1 in retail (H7)
    d->row    = -1;
    d->desc   = nullptr;
}
void InitCellNeg1Ptr0(uint8_t* d) { InitAs<UI::TableDescription::Cell, &TableCellInit>(d); }
const ArraySpec kSpec_TableCell10 = {             // 0x1800799a0
    "Array<UI::TableDescription::Cell>", 0x10, 8, &Assign16, &InitCellNeg1Ptr0,
    nullptr, true, 0, 0, 0, 0, 0, 0 };

// 0x180043d80 - UI::LayoutDescription's entry array. Element 0x38: an
// ExternalPtr<ElementDescription> at +0x00, THREE refcounted Strings at
// +0x08/+0x10/+0x18, four scalars at +0x20..+0x2f and a bool at +0x30. Stride
// and align read from the body's `capacity * 0x38` and `MOV EDX,0x8`.
//
// All three element ops are CALLS in retail and are reached BY ADDRESS here,
// through the registry, for the reason AssignGraphSeries' comment gives: a
// direct C++ call to the same instructions skips the registered entry point and
// that body's diff site never fires.
void AssignLayoutEntry(uint8_t* d, const uint8_t* s) {
    RtColl_LayoutEntryAssign_180043980(d, s);
}
// 0x180043d80's `else` (new-tail) arm, transcribed. Note the UNALIGNED QWORD of
// 1 at +0x24, which sets +0x24 to 1 AND +0x28 to 0 in one store, and that
// +0x31..+0x37 are NEVER WRITTEN.
static void LayoutEntryInit(UI::LayoutDescription::Entry* d) {
    d->desc        = nullptr;
    d->name._data        = nullptr;
    d->relativeToX._data = nullptr;
    d->relativeToY._data = nullptr;
    d->horizontal  = UI::LayoutDescription::LayoutHorizontal{2};   // DWORD
    d->vertical    = UI::LayoutDescription::LayoutVertical{1};     // UNALIGNED QWORD of 1:
    d->offsetX     = 0;                                            //   vertical = 1, offsetX = 0
    d->offsetY     = 0;                                            // DWORD
    d->force       = false;                                        // BYTE
}
void InitLayoutEntry(uint8_t* d) { InitAs<UI::LayoutDescription::Entry, &LayoutEntryInit>(d); }
// Retail has NO range helper for this element: 0x180044050 destroys ONE entry
// and both callers (this SetSize's shrink arm and ~Array at 0x180043a00) loop
// around it. The loop IS the range form.
// src/engine/collection/array_destroy.cpp ports and registers 0x180044050.
void DestroyLayoutEntryRange(void* /*ignoredThis*/, uint8_t* base, int count) {
    for (int i = 0; i < count; ++i) RtColl_DestroyElem_180044050(base + i * 0x38);
}
const ArraySpec kSpec_LayoutEntry38 = {           // 0x180043d80
    "Array<UI::LayoutDescription::Entry>", 0x38, 8, &AssignLayoutEntry,
    &InitLayoutEntry, &DestroyLayoutEntryRange, false,
    /*assign unwritten*/ 0x31, 0x38,
    /*init   unwritten*/ 0x31, 0x38,
    /*seed zero       */ 0x00, 0x20 };  // ptr + 3 String slots are dereferenced

// 0x180043b40 - UI::LayoutElement's per-cell info array, the THREE-argument
// form. Element 0x44 and align 4 (`* 0x44` at the MemAlloc, `MOV EDX,0x4`),
// copied as SEVENTEEN dwords with no refcount touch anywhere, no destroy and no
// default-tail arm - TRIVIAL.
//
// The caller only ever writes the first 0x24 bytes of the fill record it passes
// (LayoutDescription::CreateInstance, 0x1800d3568..0x1800d3581), so bytes
// 0x24..0x43 of every appended element are whatever was on that frame. That is
// the CALLER's business, not this row's: the copy is 0x44 bytes wide either way.
void AssignLayoutInfo44(uint8_t* d, const uint8_t* s) { std::memcpy(d, s, 0x44); }
const ArraySpec kSpec_LayoutInfo44 = {            // 0x180043b40
    "Array<UI::LayoutElement::Info>", 0x44, 4, &AssignLayoutInfo44,
    nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };

// 0x180075b20 — `Array<UI::DialogControllerConfigEntry>::SetSize(this, int)`.
// See AssignCfgEntry20 above for the transcription and for the +0x04..+0x07
// hole that both arms skip.
//
//   stride 0x20   SHL RCX,0x5         at 0x180075b71
//   align  8      MOV EDX,8           at 0x180075b75
//   destroy       CALL 0x180039580    (kD_Str_20at18)
//
// SEED ZEROS +0x18..+0x20: only the String slot is dereferenced. +0x00..+0x17
// is scalars copied verbatim and IS seeded non-zero -- which is exactly what
// makes the +0x04 hole detectable rather than agreeing by accident on zeros.
const ArraySpec kSpec_CfgEntry20 = {              // 0x180075b20
    "Array<DialogControllerConfigEntry:0x20>", 0x20, 8,
    &AssignCfgEntry20, &InitCfgEntry20, &DestroyCfgEntry20, false,
    /*assign unwritten*/ 0x04, 0x08,
    /*init   unwritten*/ 0x04, 0x08,
    /*seed zero       */ 0x18, 0x20 };

const ArraySpec kSpec_StringRefZ = {              // 0x180029a80
    "Array<System::String>(zero tail)", 8, 8, &AssignStringRef, &InitZero8,
    &DestroyStringRange, false,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0x00, 0x08 };

// ASSIGN WINDOW +0x04..+0x07: the colour is ONE dword and the nested Array
// starts at +0x08, so four bytes of every element are never written by anybody.
// There is no init arm (the only caller, GraphElement::AddGraph, passes a fill).
// SEED ZEROS +0x08..+0x17: the nested Array header is destroyed (its buffer
// freed) by the assign's `operator=` before being overwritten.
const ArraySpec kSpec_GraphSeries = {             // 0x1800417d0
    "Array<GraphSeries>", 0x18, 8, &AssignGraphSeries, nullptr,
    &DestroyGraphSeriesRange, false,
    /*assign unwritten*/ 0x04, 0x08,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0x08, 0x18 };

// --- 2026-09-12: the two Video::ShaderProgram instantiations ---------------
//
// Both are reached from ?Serialize@ShaderProgram@Video@@ (0x1800f5110) and its
// PlatformSerialize (0x1800f52b0) — src/engine/video/shaderprogram.cpp — and
// both were read out of their own bodies, not inferred from the element type.
//
// 0x180055db0, Array<ShaderProgram::Parameter>:
//   stride 0x10 (`SHL RCX,0x4` at 0x180055e01), align 8 (`MOV EDX,0x8`)
//   TWO-ARGUMENT: nothing in the body homes or reads R8.
//   assign  String adopt at +0x00 (`dst=0; if (src) { dst=src; ++[src-4] }`
//           at 0x180055e53..0x180055e61), then the int at +0x08 and the BYTE at
//           +0x0c. +0x0d..+0x0f is NEVER WRITTEN by either arm.
//   init    `MOV [RAX],R14(=0)` / `MOV [RAX+8],R14D` / `MOV byte [RAX+0xc],0`
//           at 0x180055e81 — the tail IS zeroed, so `init` is not null.
//   destroy 0x18004fed0, the String release loop at stride 0x10 offset 0
//           (array_destroy.cpp kD_Str_10 — the SAME spec 0x180026b50 uses, and
//           for the same reason: that row is (stride 0x10, offs {0x00}) and
//           this body computes exactly those two numbers itself).
static void ShaderParamAssign(Video::ShaderProgram::Parameter* d,
                              const Video::ShaderProgram::Parameter* s) {
    AdoptString(d->name._data, s->name._data);
    d->dest     = s->dest;                  // Parameter::dest
    d->internal = s->internal;              // Parameter::internal (a BYTE)
}
void AssignShaderParam(uint8_t* d, const uint8_t* s) {
    AssignAs<Video::ShaderProgram::Parameter, &ShaderParamAssign>(d, s);
}
static void ShaderParamInit(Video::ShaderProgram::Parameter* d) {
    d->name._data = nullptr;                // +0x00..+0x0c zeroed
    d->dest     = 0;
    d->internal = false;
}
void InitShaderParam(uint8_t* d) { InitAs<Video::ShaderProgram::Parameter, &ShaderParamInit>(d); }
void DestroyShaderParamRange(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_18004fed0(ignoredThis, base, count);
}
const ArraySpec kSpec_ShaderParam = {             // 0x180055db0
    "Array<ShaderProgram::Parameter>", 0x10, 8, &AssignShaderParam, &InitShaderParam,
    &DestroyShaderParamRange, false,
    /*assign unwritten*/ 0x0d, 0x10,
    /*init   unwritten*/ 0x0d, 0x10,
    /*seed zero       */ 0x00, 0x08 };

// 0x180053f00, Array<Collection::Array<uint8>> — an array OF arrays, which is
// the byte-code-per-backend list at ShaderProgram+0x40.
//   stride 0x10 (`SHL RCX,0x4` at 0x180053f63), align 8, TWO-ARGUMENT.
//   assign  ZEROES BOTH QWORDS FIRST (`MOV [RCX],R8(=0)` /
//           `MOV qword [RCX+8],0` at 0x180053fad) and only then, if dst != src,
//           calls Array<uint8>::operator= (0x180011220, already ported in
//           src/engine/io/modification.cpp). The zero-first is not cosmetic:
//           operator= opens with ~Array on the DESTINATION, so a non-zero
//           garbage buffer pointer there would be handed to MemFree.
//   init    the same two zero stores, no operator= — whole stride written.
//   destroy 0x18004ff80, the inner-array free loop at stride 0x10 offset 0
//           (array_destroy.cpp kD_InnerArr_10).
static void InnerArrayU8Assign(ArrayHdr* d, const ArrayHdr* s) {
    d->_data     = nullptr;
    d->_size     = 0;                    // one qword in retail (H7)
    d->_capacity = 0;
    if (d == s) return;                   // retail's `CMP RCX,RSI; JE` skip
    RtIO::RtColl_ArrayU8Assign_180011220(d, s);
}
void AssignInnerArrayU8(uint8_t* d, const uint8_t* s) {
    AssignAs<ArrayHdr, &InnerArrayU8Assign>(d, s);
}
void InitInnerArrayU8(uint8_t* d) {
    std::memset(d, 0, 0x10);
}
void DestroyInnerArrayU8Range(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyInner_18004ff80(ignoredThis, base, count);
}
const ArraySpec kSpec_InnerArrayU8 = {            // 0x180053f00
    "Array<Array<uint8>>", 0x10, 8, &AssignInnerArrayU8, &InitInnerArrayU8,
    &DestroyInnerArrayU8Range, false,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0x00, 0x10 };

// ============================================================================
// CUTOVER CLOSURE BATCH 2b (2026-09-12) — eleven more instantiations
// ============================================================================
//
// All eleven came off docs/history/runtime_port/closure/WORKLIST.md in one batch and
// every parameter below was read out of the ADDRESS's OWN body (stride from the
// index arithmetic, align from its `MOV EDX,<n>` into MemAlloc, the assign and
// init arms from the copy loop, the destroy from the CALL on the realloc and
// shrink arms). Nothing here is inherited from a lookalike row: this family has
// now produced five wrong answers by resemblance (alignment immediate, shift,
// trailing call, missing else-arm, and batch 2's align-4 emitter array).
//
// The element types that are NOT trivially copyable reach their per-element
// helper by ADDRESS (ResolveAddr consults the port registry first), exactly as
// kSpec_PkgEntry and kSpec_MaterialParam already do.
// ============================================================================

// --- 0x1800136f0  Array<Graphics::EdgeMesh::Triangle>, stride 0x18, ALIGN 4 --
// The TriangleList array `Graphics::EdgeMesh` serialises through
// SerializeTriangleListArray (0x180014850, ported in this batch). Retail writes
// SIX DWORDS, not three qwords — transcribed as dwords so a watchpoint sees
// retail's store widths. NO ELSE ARM: the copy loop's body is guarded by
// `if (i < oldSize)` with nothing after it, so a grow past the old size leaves
// the new tail UNINITIALISED (this is why it cannot reuse kSpec_Node18, which
// also differs on align: 4 here, 8 there).
void Assign18d(uint8_t* d, const uint8_t* s) { AssignAs<uint32_t, &CopyWords<uint32_t, 6>>(d, s); }
const ArraySpec kSpec_Tri18A4 = {                 // 0x1800136f0
    "Array<EdgeMesh::Triangle>", 0x18, 4, &Assign18d,
    nullptr,   // NEW TAIL LEFT UNINITIALISED — retail's loop has no else arm
    nullptr, true, 0, 0, 0, 0, 0, 0 };

// --- 0x18005cc60  Array<T*>, stride 8, align 8, tail ZEROED ------------------
// Two-argument. Assign is a PLAIN qword move (no zero-first store), so it is
// Assign8 and NOT Assign8Z — the distinction kSpec_PtrZero exists for. The
// else arm is one qword zero store. Parameter-identical to kSpec_U64Zero
// ("Array<Time>"); given its own row because the name is what the diff summary
// prints and "Array<Time>" on a pointer array is the kind of label that gets
// believed.
const ArraySpec kSpec_Ptr8Zero = {                // 0x18005cc60
    "Array<T*>(zero tail)", 8, 8, &Assign8, &InitZero8, nullptr, true,
    0, 0, 0, 0, 0, 0 };

// --- 0x180053da0  Array<uint8>, stride 1, align 1, three-argument ------------
// The compiled-shader BLOB array's inner byte buffer (Video::*::PlatformSerialize
// reaches it through 0x180053ea0). `MemAlloc(cap, 1)` — no shift at all.
const ArraySpec kSpec_Fill1 = {                   // 0x180053da0
    "Array<uint8>", 1, 1, &Assign1, nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };

// --- 0x180063680  Array<{7 qwords}>, stride 0x38, align 8, three-argument ----
// Seven plain qword moves at +0x00..+0x30; no refcount, no zero-first store,
// no else arm (every caller passes a fill).
void Assign38(uint8_t* d, const uint8_t* s) { AssignAs<uint64_t, &CopyWords<uint64_t, 7>>(d, s); }
const ArraySpec kSpec_Fill38 = {                  // 0x180063680
    "Array<blk56>", 0x38, 8, &Assign38, nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };

// --- 0x180061880  Array<{String*, qword}>, stride 0x10, tail ZEROED ----------
// The TWO-ARGUMENT sibling of kSpec_StringExt (0x180061480). Same element, same
// AssignStringExt, same destroy helper (0x180005930, Body B / kD_Str_10) — and
// one difference that is the whole reason for a separate row: this body HAS an
// else arm, `MOV [R..],0 ; MOV [R..+8],0`, so the new tail is zeroed. Reusing
// kSpec_StringExt (init = nullptr) would leave a grown tail holding whatever
// the allocator returned, and the first destroy would then release a garbage
// String pointer.
const ArraySpec kSpec_StringExtZ = {              // 0x180061880
    "Array<{String*,ExternalPtr}>(zero tail)", 0x10, 8,
    &AssignStringExt, &InitZero10, &DestroyStringExt, false,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0x00, 0x08 };

// --- 0x180034010 / 0x180033080  Array<Save::MetaDataEntry>, stride 0x20 ------
// Two addresses, ONE spec: 0x180034010 is the two-argument form and 0x180033080
// the three-argument one, and their copy loops are the same instructions. Both
// are reached from IO::Persistence's LoadMetaData (0x1800c1b30) / SaveMetaData
// (0x1800c1490) / UpdateList (0x1800c1ea0).
//
// Element, read from 0x180034010's loop at its assign arm:
//     +0x00  String*  zero-then-adopt-then-INC [str-4]
//     +0x08  String*  same
//     +0x10  int32    plain dword
//     +0x14  int32    plain dword
//     +0x18  int32    plain dword
//     +0x1c..+0x1f    NEVER WRITTEN BY EITHER ARM
// Init arm (0x180034010 only): the two STRING qwords zeroed and nothing else —
// so the init window is +0x10..+0x20, wider than the assign's.
// Destroy: CALL 0x1800341e0 on both the realloc arm and the shrink tail
// (array_destroy.cpp's `kD_Str2_20`, ported in this same commit).
using SaveModInfo = Save::SaveLoad::ModInfo;
static void ModInfoAssign(SaveModInfo* d, const SaveModInfo* s) {
    AdoptString(d->name._data, s->name._data);
    AdoptString(d->path._data, s->path._data);
    d->versionMajor = s->versionMajor;
    d->gameVersion  = s->gameVersion;
    d->versionMinor = s->versionMinor;
}
void AssignMetaEntry20(uint8_t* d, const uint8_t* s) { AssignAs<SaveModInfo, &ModInfoAssign>(d, s); }
static void ModInfoInit(SaveModInfo* d) {
    d->name._data = nullptr;
    d->path._data = nullptr;
}
void InitMetaEntry20(uint8_t* d) { InitAs<SaveModInfo, &ModInfoInit>(d); }
void DestroyMetaEntry20Range(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_1800341e0(ignoredThis, base, count);
}
const ArraySpec kSpec_MetaEntry20 = {             // 0x180034010, 0x180033080
    "Array<Save::MetaDataEntry>", 0x20, 8,
    &AssignMetaEntry20, &InitMetaEntry20, &DestroyMetaEntry20Range, false,
    /*assign unwritten*/ 0x1c, 0x20,
    /*init   unwritten*/ 0x10, 0x20,
    /*seed zero       */ 0x00, 0x10 };

// --- 0x1800332d0  Array<Save::MetaData>, stride 0x58, three-argument ---------
// Element assign is a CALL to the copy constructor 0x180032f70 (ported in this
// batch, src/engine/save/save_metadata.cpp) and the per-element destroy is a
// CALL to 0x1800c3f40 IN A LOOP — retail has no range helper here, it writes the
// loop out at both arms. The wrapper below is that loop; the order is ascending
// at both arms, read from the two `ADD R..,0x58` walks.
static void MetaData58Destroy(Save::SaveLoad::SaveInfo* base, int count) {
    for (int i = 0; i < count; ++i) {                     // ~SaveInfo, 0x1800c3f40
        base[i].Save::SaveLoad::SaveInfo::~SaveInfo();
    }
}
void DestroyMetaData58Range(void* ignoredThis, uint8_t* base, int count) {
    DestroyAs<Save::SaveLoad::SaveInfo, &MetaData58Destroy>(ignoredThis, base, count);
}
static void MetaData58Assign(Save::SaveLoad::SaveInfo* d, const Save::SaveLoad::SaveInfo* s) {
    ::new (static_cast<void*>(d)) Save::SaveLoad::SaveInfo(*s);   // the copy ctor 0x180032f70
}
void AssignMetaData58(uint8_t* d, const uint8_t* s) { AssignAs<Save::SaveLoad::SaveInfo, &MetaData58Assign>(d, s); }
const ArraySpec kSpec_MetaData58 = {              // 0x1800332d0
    "Array<Save::MetaData>", 0x58, 8,
    &AssignMetaData58, nullptr, &DestroyMetaData58Range, false,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0x00, 0x58 };

// --- 0x1800638e0  Array<Graphics::DrawGroup>, stride 0x1f0, three-argument ---
// Element assign is the copy constructor 0x18001be60 (ported in this batch);
// the per-element destroy is MSVC's `eh vector destructor iterator` over THREE
// inner `Collection::Array<T>` at +0x1c0/+0x1d0/+0x1e0, i.e. exactly
// array_destroy.cpp's `kD_InnerArr3_1f0` (J2b's 0x18001c8c0 row), destroyed in
// DESCENDING slot order. Written out here with ArrayDtorImpl rather than
// reaching array_destroy.cpp, because ArrayDtorImpl IS the body the iterator
// eventually calls (0x18001c650, the twelfth copy of J1).
// B.4: the element is the typed Graphics::RendererDrawGroup (Camera.h); its
// three item arrays are destroyed by name. RendererDrawItem is trivially
// destructible, so each `~Array` is the J1 body alone (ArrayDtorImpl).
static void DrawGroup1f0Destroy(::Graphics::RendererDrawGroup* base, int count) {
    for (int i = 0; i < count; ++i) {
        base[i]._items[2].~Array();   // reverse order,
        base[i]._items[1].~Array();   // as the CRT
        base[i]._items[0].~Array();   // iterator does
    }
}
void DestroyDrawGroup1f0Range(void* ignoredThis, uint8_t* base, int count) {
    DestroyAs<::Graphics::RendererDrawGroup, &DrawGroup1f0Destroy>(ignoredThis, base, count);
}
void AssignDrawGroup1f0(uint8_t* d, const uint8_t* s) {
    RtGraphics_DrawGroup_CopyCtor(d, s);
}
const ArraySpec kSpec_DrawGroup1f0 = {            // 0x1800638e0
    "Array<Graphics::DrawGroup>", 0x1f0, 8,
    &AssignDrawGroup1f0, nullptr, &DestroyDrawGroup1f0Range, false,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0x00, 0x1f0 };

// --- 0x180051020  Array<Audio::Channel>, stride 0x70, n FOLDED TO 256 --------
// A one-argument body like 0x18007b960: `CMP E..,0x100`, `MOV [RCX+8],0x100`,
// `MOV [RCX+0xc],0x300` (the folded 3*256). Assign is fourteen plain qword
// moves. The else arm is NOT a memset — it writes a specific pattern with two
// 1.0f floats in it, and it SKIPS +0x0c..+0x0f and +0x6d..+0x6f:
//     +0x00 q=0   +0x08 d=0   +0x10 q=0   +0x18 d=1.0f  +0x1c d=1.0f
//     +0x68 d=0   +0x6c b=0   +0x20..+0x67 q=0 (nine qwords)
// Transcribed in retail's own store order.
void Assign70(uint8_t* d, const uint8_t* s) { AssignAs<uint64_t, &CopyWords<uint64_t, 14>>(d, s); }
static void AudioChannelInit(Audio::Scene::Channel* d) {
    d->soundEffectRef    = nullptr;     // +0x00 q=0
    d->soundType         = 0;           // +0x08 d=0
    d->startTime._ticks  = 0;           // +0x10 q=0
    d->volume            = 1.0f;        // +0x18 d=1.0f
    d->sampleVolume      = 1.0f;        // +0x1c d=1.0f
    d->handleTag         = 0;           // +0x68 d=0
    d->started           = 0;           // +0x6c b=0
    for (int k = 0; k < 8; ++k)         // +0x20..+0x5f q=0 (eight qwords)
        d->platformChannel[k] = nullptr;
    for (int k = 0; k < 8; ++k)         // +0x60..+0x67 (the ninth qword)
        d->channelFlags[k] = 0;
}
void InitAudioChannel70(uint8_t* d) { InitAs<Audio::Scene::Channel, &AudioChannelInit>(d); }
const ArraySpec kSpec_AudioChannel70 = {          // 0x180051020
    "Array<Audio::Channel>", 0x70, 8, &Assign70, &InitAudioChannel70, nullptr,
    true,
    /*assign unwritten*/ 0,    0,
    // The init arm leaves +0x0c..+0x0f and +0x6d..+0x6f unwritten; ArraySpec
    // carries ONE window per arm, so the first is recorded and the second is
    // stated here. Neither is compared by anything: no oracle in this tree
    // constructs an Audio::Channel array.
    /*init   unwritten*/ 0x0c, 0x10,
    /*seed zero       */ 0,    0 };

// ============================================================================
// VTABLE BATCH 7b (2026-09-12) — the six Array<T>::SetSize bodies the batch-7
// Graphics/Audio serializers reach BY ADDRESS. Every row below was read out of
// its own disassembly (retail Runtime-x64.dll, Ghidra disassemble_function):
// `stride` is the SHL/IMUL feeding MemAlloc, `align` the immediate in EDX, and
// the element ops are the loop body — never inferred from the element type.
//
//   0x180059e70  Array<AnimationGroup::Clip>      stride 0x70 align 8  NON-TRIV
//   0x18005a220  Array<{String,String,float}>     stride 0x18 align 8  NON-TRIV
//   0x18005a410  Array<float>                     stride 4    align 4  = kSpec_I32
//   0x180059930  Array<{int32,ExternalPtr}>       stride 0x10 align 8
//   0x180021220  Array<ParticlePoolRecord*>       stride 8    align 8  = kSpec_Fill8
//   0x180020e90  Array<{Array<T>,int32}>          stride 0x14 align 4
//
// The last two are the "pool rebuild" pair `?Serialize@ParticleSystem@Graphics@@`
// calls at 0x1800a507a and 0x1800a50eb. *** 0x180020e90 IS NOT A RECORD
// CONSTRUCTOR. *** vtbatch7_animgroup_particlesystem.cpp used to call it
// `recCtor`; it is this family's SetSize over the record's OWN inner array
// header at record+0x00, with `ParticleSystem::_f28` as the new size. That
// comment is corrected at its source in the same commit (PLAN.md §6 step 8).
// ----------------------------------------------------------------------------

// --- 0x180059e70  Array<Graphics::AnimationGroup::Clip>, stride 0x70 --------
// assign   CALL 0x1800160a0   (the clip's own operator=, reached by ADDRESS so
//                              the port registry gets first refusal)
// init     the inline default-construct at 0x180059f2d..0x180059f7b
// destroy  CALL 0x1800fb290, ONE ELEMENT PER CALL, in a loop on both arms
//          (0x180059fa3 realloc arm, 0x18005a005 shrink arm) — unlike every
//          other destroy row in this file, which takes (base, count).
void AssignAnimClip70(uint8_t* d, const uint8_t* s) {
    RtGraphics_Animation_CopyCtor(d, s);
}
static void AnimClipInit(Graphics::Animation* d) {
    d->_events._data     = nullptr;  // 180059f2d  inner event array
    d->_events._size     = 0;        // 180059f30  (one qword, H7)
    d->_events._capacity = 0;
    d->_name._data       = nullptr;  // 180059f38  three String slots
    d->_meshName._data   = nullptr;  // 180059f3c
    d->_sourceName._data = nullptr;  // 180059f40
    d->_instanceCount    = 1;        // 180059f44  NOT zero
    d->_instanced        = false;    // 180059f4b  byte, not dword
    d->_startFrame       = 0;        // 180059f4f  (one qword, H7)
    d->_endFrame         = 0;
    d->_rate             = 0.0f;     // 180059f57
    d->_looping          = false;    // 180059f5b  byte, not dword
    d->_loopFrame        = 0;        // 180059f5f  (one qword, H7)
    d->_frameRate        = 0.0f;
    d->_loopScale        = 0.0f;     // 180059f67  (one qword, H7)
    d->_boneCount        = 0;
    d->_frameCount       = 0;        // 180059f6f
    d->_duration         = 0;        // 180059f73
    d->_keys._data       = nullptr;  // 180059f77  pose array header
    d->_keys._size       = 0;        // 180059f7b  (one qword, H7)
    d->_keys._capacity   = 0;
}
void InitAnimClip70(uint8_t* d) { InitAs<Graphics::Animation, &AnimClipInit>(d); }
void DestroyAnimClip70(void* /*ignoredThis*/, uint8_t* base, int count) {
    for (int i = 0; i < count; ++i) RtGraphics_Animation_Dtor(base + static_cast<int64_t>(i) * 0x70);
}
// THREE init holes, not one, and ArraySpec can name only one window. The one
// named is the widest (+0x54..+0x57, the padding before the int64 at +0x58);
// the other two are +0x2d..+0x2f and +0x3d..+0x3f, the padding after each of
// the two BYTE stores above. All three are retail's holes, recorded here rather
// than zeroed (reference_oracle_must_not_compare_unwritten_bytes.md). Nothing
// drives this row through array_probe.cpp's table today, so the window is
// documentation; it becomes load-bearing the day someone adds the row there.
const ArraySpec kSpec_AnimClip70 = {              // 0x180059e70
    "Array<Graphics::AnimationGroup::Clip>", 0x70, 8,
    &AssignAnimClip70, &InitAnimClip70, &DestroyAnimClip70, false,
    /*assign unwritten*/ 0,    0,      // an opaque called operator=; no claim
    /*init   unwritten*/ 0x54, 0x58,   // plus 0x2d..0x2f and 0x3d..0x3f
    /*seed zero       */ 0x00, 0x70 }; // every slot is dereferenced by the ops

// --- 0x18005a220  Array<{String,String,float}>, stride 0x18 -----------------
// AnimationGroup's +0x38 tag table. assign is AssignStringPair10's two
// refcounted adopts (0x18005a2c8..0x18005a2ea, byte-identical shape) plus one
// dword at +0x10; init zeroes both String slots and seeds the float with
// 0x3dcccccd == 0.1f (0x18005a30a) — the SAME default AnimationGroup's own ctor
// gives its +0x30. destroy is `CALL 0x18001cbf0` with (base, count) on both
// arms (0x18005a328 realloc, 0x18005a37b shrink).
// B.4 (X-RL): the element is `Graphics::AnimationGroup::Tag` (AnimationGroup.h).
static void AnimTagAssign(Graphics::AnimationGroup::Tag* d,
                          const Graphics::AnimationGroup::Tag* s) {
    AdoptString(d->_string00._data, s->_string00._data);            // 18005a2c8 (as AssignStringPair10)
    AdoptString(d->_string08._data, s->_string08._data);
    d->_float10 = s->_float10;                                      // 18005a2ed
}
void AssignTagStrStrF18(uint8_t* d, const uint8_t* s) {
    AssignAs<Graphics::AnimationGroup::Tag, &AnimTagAssign>(d, s);
}
static void AnimTagInit(Graphics::AnimationGroup::Tag* d) {
    d->_string00._data = nullptr;                                   // 18005a303
    d->_string08._data = nullptr;                                   // 18005a306
    d->_float10       = 0.1f;                                       // 18005a30a  0x3dcccccd
}
void InitTagStrStrF18(uint8_t* d) {
    InitAs<Graphics::AnimationGroup::Tag, &AnimTagInit>(d);
}
void DestroyTagStrStrF18(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_18001cbf0(ignoredThis, base, count);
}
const ArraySpec kSpec_TagStrStrF18 = {            // 0x18005a220
    "Array<{String,String,float}>", 0x18, 8,
    &AssignTagStrStrF18, &InitTagStrStrF18, &DestroyTagStrStrF18, false,
    /*assign unwritten*/ 0x14, 0x18,   // the element is 0x14 wide in a 0x18 slot
    /*init   unwritten*/ 0x14, 0x18,
    /*seed zero       */ 0x00, 0x10 }; // both String slots are dereferenced

// --- 0x180059930  Array<{int32 tag, ExternalPtr<AnimationEvent>}> -----------
// The nested per-clip event array. stride 0x10 (`SHL RCX,0x4`), align 8
// (`MOV EDX,0x8`), assign is two plain qword copies, and the init arm writes
// only a DWORD at +0x00 and a QWORD at +0x08 (0x1800599d4/0x1800599d7), so
// +0x04..+0x07 is left holding whatever the allocation contained. No destroy
// call anywhere in the body — the ExternalPtr is not released on a shrink.
static void AnimEventInit(Graphics::Animation::Event* d) {
    d->_frame = 0;                   // DWORD
    d->_event = nullptr;             // QWORD at +0x08
}
void InitIdExtPtr10(uint8_t* d) { InitAs<Graphics::Animation::Event, &AnimEventInit>(d); }
const ArraySpec kSpec_IdExtPtr10 = {              // 0x180059930
    "Array<{int32,ExternalPtr<AnimationEvent>}>", 0x10, 8,
    &Assign16, &InitIdExtPtr10, nullptr, true,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0x04, 0x08,
    /*seed zero       */ 0,    0 };

// --- 0x180020e90  Array<{Array<T> header, int32}>, stride 0x14, ALIGN 4 -----
// The inner array of one ParticleSystem pool record. FIVE dword copies
// (0x180020f71..0x180020f93 and its three unrolled twins), and an init arm that
// writes a QWORD then a DWORD — 12 of the 20 bytes — leaving +0x0c..+0x13
// uninitialised. Align is 4 (`MOV EDX,R14D` with R14D seeded 4 at 0x180020ece),
// NOT 8, which is why it cannot borrow any stride-0x14-adjacent row.
void Assign14(uint8_t* d, const uint8_t* s) { AssignAs<uint32_t, &CopyWords<uint32_t, 5>>(d, s); }
static void ParticleExtraInit(RtGraphics::ParticleExtra* d) {
    d->_velX = 0.0f;                 // 180020f9a  (one qword, H7)
    d->_velY = 0.0f;
    d->_velZ = 0.0f;                 // 180020f9d
}
void InitZero14Lo12(uint8_t* d) { InitAs<RtGraphics::ParticleExtra, &ParticleExtraInit>(d); }
const ArraySpec kSpec_Rec14 = {                   // 0x180020e90
    "Array<{Array,int32}:0x14>", 0x14, 4,
    &Assign14, &InitZero14Lo12, nullptr, true,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0x0c, 0x14,
    /*seed zero       */ 0,    0 };

}  // namespace rtcoll

// ============================================================================
// The two bodies, with the per-address plumbing around them.
// ============================================================================
namespace rtcoll {

// ENDGAME B.3.2 step 1: this is now one call into
// `Collection::Array<uint8_t>::~Array`, which is `ArrayDtorImpl` plus the
// element-destroy step a trivial element does not have. The post-state is the
// whole observable effect on the object — retail's last two stores are
// `_data = 0` and one qword covering _size and _capacity — and a WRONG pointer
// handed to Free is not visible here at all; it is caught by
// BANISHED_REDZONE=watch, which is why the recommended J1 run pairs the two.
//
// The probe-era second `ArrayDtorImpl(a)` and the two snprintf buffers nothing
// read are gone with the call they annotated. The second call was idempotent
// (`MemFree(nullptr)` is a no-op inside 0x1800c7ad0), so dropping it changes no
// state; it was there to format the "ours vs theirs" line of a diff oracle that
// no longer runs.
void ArrayDtorSited(ArrayHdr* a) {
    a->~Array();
}

// ---------------------------------------------------------------------------
// WAVE 9 LANE DESTROY — arm coverage for kSpec_PkgEntry's destroy step
// ---------------------------------------------------------------------------
// 0x180026b50 is called from TWO different branches of 0x1800298f0 and a run
// that only ever took one of them has verified one of them. Nothing OUTSIDE
// this function can tell the two apart -- the callee is handed (base, count) in
// both cases, and `base == old` vs `base == old + newSize*stride` is not
// distinguishable at the callee when newSize is 0. So the count is taken HERE,
// at the branch itself (WAVE9.md "count branches taken INSIDE the function
// under test, not proxies observed from outside it").
//
// The core. `fill` non-null selects the three-argument form; otherwise the
// spec's `init` (which may itself be null) supplies the new tail.
void SetSizeCore(ArrayHdr* a, int newSize, const uint8_t* fill,
                 const ArraySpec* spec) {
    const uint32_t stride = spec->stride;

    // NOT RETAIL (review F-012, UB): retail 0x1800298f0 has no sign check, and
    // every array Serialize hands it the count read from the stream. A negative
    // newSize skips the grow, then the shrink arm destroys `oldSize - newSize`
    // elements from `old + newSize*stride` (below the buffer: pointer arithmetic
    // out of the object, a wild destroy/free) and leaves _size negative. Clamped
    // to 0 here; no valid data reaches it, so retail's behaviour is unchanged.
    if (newSize < 0) {
        newSize = 0;
    }

    const int32_t oldSizeI = a->_size;
    const int64_t oldSize  = static_cast<int64_t>(oldSizeI);   // MOVSXD
    if (newSize == oldSizeI) {
        // Retail's exact-equal early return. Counted so the probe and the live
        // summary can tell "entered" from "did work" — a shadow placed after an
        // early return makes calls= mean the wrong thing
        // (reference_probe_needs_an_effectiveness_control.md).
        return;
    }

    uint8_t* const old    = a->_data;
    const int32_t  oldCap = a->_capacity;
    a->_size = newSize;

    if (newSize > a->_capacity) {                 // signed 32-bit
        a->_capacity = newSize;
        // 32-bit LEA arithmetic, wrap included.
        if (newSize == static_cast<int32_t>(oldSizeI + 1) && oldSizeI != 0)
            a->_capacity = static_cast<int32_t>(newSize * 3);
        a->_data = static_cast<uint8_t*>(
            MemAlloc(static_cast<uint64_t>(static_cast<int64_t>(a->_capacity)) * stride,
                     spec->align));               // MOVSXD then SHL/IMUL
    }

    const bool realloced = (old != a->_data);
    const int64_t start = realloced ? 0 : oldSize;

    // DRAWSORT target 1 (NOT RETAIL, faithful perf work; setsize_prof.h): with no
    // fill and no init, an iteration at i >= oldSize computes `dst`, tests it and
    // does nothing else, so the loop can stop at min(newSize, oldSize) with the
    // same bytes, header and allocator calls. `end` is computed once per call.
    const bool emptyTail = (fill == nullptr && spec->init == nullptr);
    const int64_t end = (SetSizeFast::kSkipEmptyTail && emptyTail && oldSize < newSize)
                            ? oldSize
                            : static_cast<int64_t>(newSize);
    if (g_setSizeProfOn) [[unlikely]] {
        const int64_t iters = (newSize > start) ? newSize - start : 0;
        const int64_t tailFrom = (start > oldSize) ? start : oldSize;
        const int64_t empty = (emptyTail && newSize > tailFrom) ? newSize - tailFrom : 0;
        SetSizeProf_Record(spec, oldSizeI, newSize, iters, empty, realloced);
    }

    for (int64_t i = start; i < end; ++i) {
        uint8_t* dst = a->_data + i * stride;
        if (dst == nullptr) continue;             // retail tests the COMPUTED address
        if (i < oldSize)      spec->assign(dst, old + i * stride);
        else if (fill)        spec->assign(dst, fill);
        else if (spec->init)  spec->init(dst);
        // else: retail's else arm is empty and the new tail stays uninitialised
    }

    // The one independent check available at a live call site: on a realloc the
    // copied prefix must equal what is still sitting in the old buffer, and the
    // old buffer is not freed yet. This does NOT re-derive our own rule — it
    // compares against the source data — but it only means anything when the
    // element assignment is a pure byte copy, so `byteCopy` gates it.

    if (realloced) {
        if (spec->destroy) {
            spec->destroy(nullptr, old, oldSizeI);
        }
        MemFree(old);                             // null-guarded inside 0x1800c7ad0
    } else if (spec->destroy && oldSizeI > newSize) {
        spec->destroy(nullptr, old + static_cast<int64_t>(newSize) * stride,
                      oldSizeI - newSize);
    }

    // Header algebra. WEAK BY CONSTRUCTION (it restates the rule this function
    // implements) and kept for its coverage value: a non-zero calls= on this
    // site is what says the address was reached at all.
}

// ---------------------------------------------------------------------------
// DRAWSORT target 1 -- the SetSizeCore COMPPROF counter (NOT RETAIL; see
// setsize_prof.h). One small table keyed by spec pointer, written only on the
// thread the harness marked, read and reset by the harness on that same thread.
// ---------------------------------------------------------------------------
int g_setSizeProfOn = 0;

namespace {
constexpr int kSetSizeProfRows = 64;
SetSizeProfRow s_setSizeProf[kSetSizeProfRows];
int            s_setSizeProfN = 0;
thread_local bool t_setSizeProfThread = false;
}  // namespace

void SetSizeProf_MarkThisThread() { t_setSizeProfThread = true; }

void SetSizeProf_Record(const ArraySpec* spec, int32_t oldSize, int32_t newSize,
                        int64_t iters, int64_t emptyIters, bool realloced) {
    if (!t_setSizeProfThread) return;
    SetSizeProfRow* row = nullptr;
    for (int i = 0; i < s_setSizeProfN; ++i)
        if (s_setSizeProf[i].spec == spec) { row = &s_setSizeProf[i]; break; }
    if (row == nullptr) {
        if (s_setSizeProfN >= kSetSizeProfRows) return;
        row = &s_setSizeProf[s_setSizeProfN++];
        *row = SetSizeProfRow{};
        row->spec = spec;
    }
    const int cls = (newSize == oldSize + 1) ? kSetSizeAppend
                  : (newSize == 0)           ? kSetSizeClear
                  : (oldSize == 0)           ? kSetSizeGrow0
                  : (newSize > oldSize)      ? kSetSizeGrow
                                             : kSetSizeShrink;
    ++row->calls[cls];
    row->iters[cls] += static_cast<uint64_t>(iters);
    row->emptyIters += static_cast<uint64_t>(emptyIters);
    if (realloced) ++row->reallocs;
    if (newSize > row->maxNewSize) row->maxNewSize = newSize;
}

int SetSizeProf_Take(SetSizeProfRow* out, int maxRows, uint64_t* totalCalls,
                     uint64_t* totalIters, uint64_t* totalEmpty) {
    uint64_t c = 0, it = 0, em = 0;
    for (int i = 0; i < s_setSizeProfN; ++i) {
        for (int k = 0; k < kSetSizeClasses; ++k) {
            c  += s_setSizeProf[i].calls[k];
            it += s_setSizeProf[i].iters[k];
        }
        em += s_setSizeProf[i].emptyIters;
    }
    if (totalCalls) *totalCalls = c;
    if (totalIters) *totalIters = it;
    if (totalEmpty) *totalEmpty = em;
    // Largest iteration count first (a selection of the top rows; the table is small).
    auto rowIters = [](const SetSizeProfRow& r) {
        uint64_t s = 0;
        for (int k = 0; k < kSetSizeClasses; ++k) s += r.iters[k];
        return s;
    };
    int n = 0;
    bool taken[kSetSizeProfRows] = {};
    while (n < maxRows) {
        int best = -1;
        for (int i = 0; i < s_setSizeProfN; ++i)
            if (!taken[i] && (best < 0 || rowIters(s_setSizeProf[i]) > rowIters(s_setSizeProf[best])))
                best = i;
        if (best < 0) break;
        taken[best] = true;
        out[n++] = s_setSizeProf[best];
    }
    s_setSizeProfN = 0;
    return n;
}

const char* SetSizeProf_SpecName(const ArraySpec* spec) {
    return (spec && spec->name) ? spec->name : "?";
}

// ---------------------------------------------------------------------------
// exe 0x140017550 — `Array<T>::Find(const T&)` over an 8-byte element (B9 RT, D5;
// was BuildComponent_FindInQueue, Component/buildcomponent.cpp). RCX the array,
// RDX the key:
//   XOR EBX,EBX / CMP [RCX+8],EBX / JLE -> -1
//   loop: MOV RDI,[R14] / CALL [CheckPtr](key) / MOV RAX,[R15] / CMP [RDI+RSI],RAX
//         / JZ -> return i;  INC EBX / CMP EBX,[R14+8] / JL
//   OR EAX,-1
// The key's CheckPtr is a bare RET (reference_checkptr_is_a_ret_in_every_build).
// ---------------------------------------------------------------------------
int ArrayFind8(const ArrayHdr* a, const void* key) {
    const uint64_t* const k = static_cast<const uint64_t*>(key);   // the 8-byte element  // CAST-OK: elem_stride
    int count = a->_size;
    for (int i = 0; i < count; i++) {
        const uint64_t* base = reinterpret_cast<const uint64_t*>(a->_data);   // the 8-byte element view  // CAST-OK: elem_stride
        if (base[i] == *k) {
            return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// exe 0x140002cc0 — `Array<T>::Remove(const T&)` over an 8-byte element (B9 RT,
// D5; was CemeteryListErase_Reimpl, Component/cemeterycomponent.cpp): removes
// EVERY element equal to the key. Per match the tail shifts down one slot with
// retail's `MOV [RDX],RBP / MOV RAX,[RDX+8] / MOV [RDX],RAX` (a dead zero store,
// then the copy) behind the computed-address test, `DEC dword ptr [RBX+8]`, and
// the index steps back so the shifted-in element is compared too.
// ---------------------------------------------------------------------------
void ArrayRemove8(ArrayHdr* a, const void* key) {
    const int64_t* const k = static_cast<const int64_t*>(key);   // the 8-byte element  // CAST-OK: elem_stride
    int i = 0;
    while (i < a->_size) {
        const int64_t* data = reinterpret_cast<const int64_t*>(a->_data);   // the 8-byte element view  // CAST-OK: elem_stride
        if (data[i] == *k) {
            for (int j = i + 1; j < a->_size; ++j) {
                int64_t* slot = reinterpret_cast<int64_t*>(a->_data) + (j - 1);   // the 8-byte element view  // CAST-OK: elem_stride
                if (slot) slot[0] = slot[1];
            }
            a->_size = a->_size - 1;
            --i;
        }
        ++i;
    }
}

}  // namespace rtcoll

// ============================================================================
// The 27 registered entry points.
//
// One free function per ADDRESS: a tail call into the shared body, so that each
// registered address has its own entry point.
//
// The naming is `RtColl_<Job>_<address>` on purpose: when a crash log names one
// of these, the address is right there.
// ============================================================================

namespace rtcoll {

#define J1_ENTRY(ADDR)                                                        \
    void* RtColl_ArrayDtor_##ADDR(void* a) {                                  \
        ArrayDtorSited(static_cast<ArrayHdr*>(a));  /* CAST-OK: elem_stride (block-comment marker: a macro line cannot end in //) macro: the erased entry views its caller's Array<T> (void*) as the ArrayHdr core */ \
        return nullptr;                                                       \
    }

J1_ENTRY(180013da0)   // Graphics:    Material::_programs, ~Material's call (2026-09-12 cutover closure)
J1_ENTRY(18001c750)   // Graphics:    Array<int32>, the Scene dtor's four middle calls
J1_ENTRY(180051d80)   // Audio:       Scene's channel array
// WAVE 9 LANE UIF, 2026-09-08 — THE TWELFTH ADDRESS. Lane D's survey found
// eleven; there are twelve. 0x180037240 is `UI::Element::_children`'s
// `~Array<Element*>`, the call at 0x1801161f7 in `??1Element@UI@@UEAA@XZ` on
// `this+0xa0`. It was invisible to the survey because nothing in our tree
// called it until this lane ported that destructor.
//
// It is the SAME BODY, and that is a byte-level claim, not an eyeball one:
// 0x180037240 and 0x180033960 were compared over all 115 bytes and differ at
// exactly eleven offsets (39,40, 60,61, 71,72, 76,77,78, 86,87) — every one of
// them inside a RIP-relative displacement or a CALL rel32, and every one
// resolving to the same four absolute operands the J1 note above pins
// (0x1801fc718, 0x18014b390, 0x1800c6c00, 0x18014b388). Same instructions,
// same immediates, same targets, so the "no parameters at all, not even a
// stride" conclusion covers it unchanged and one body still serves them all.
// WAVE 11 LANE OBJLIFE, 2026-09-09 — THE SEVENTEENTH ADDRESS, and the note
// above predicted it: 0x18002fb70 is `Object::InternalFactory::_objects`'
// `~Array<Internal*>`, the TAIL JUMP of `??1InternalFactory@Object@@` at
// 0x18002f888. It was invisible to every previous survey for the standing
// reason — nothing in our tree called it until this lane ported that dtor.
//
// SAME BODY, measured rather than eyeballed. Both bodies were disassembled and
// every RIP displacement and rel32 resolved to its ABSOLUTE target before
// comparing (a raw byte-diff of two copies of one function reports differences
// that are only distances — reference_position_only_diffs_hide_one_body):
// 29 instructions on each side, 29 identical, and the ONE textual difference
// is an internal forward branch whose ENCODED displacement is the same byte
// (0x36) on both sides. The four external operands resolve identically to
// 0x1801fc718, 0x18014b390, 0x1800c6c00 and 0x18014b388 — the same four the
// J1 note above pins.
// WAVE 9 LANE UIW, 2026-09-08 — THE THIRTEENTH AND FOURTEENTH ADDRESSES, both
// reached by `?RemoveAllItems@ComboElement@UI@@` (0x180075120): its second and
// third calls, on `_sprites` (+0xf8) and `_userData` (+0x108).
//
// Same byte-level claim as the twelfth, made the same way and not by eye. Both
// were read with `read_memory` over all 115 bytes and compared against
// 0x180033960. 0x180037310 differs at exactly eleven offsets and 0x180024aa0 at
// exactly fifteen; EVERY differing byte in both lies inside one of the five
// relocated operands (the disp32 of `MOV RBX,[rip]`, `CALL [rip]`,
// `MOV RCX,[rip]`, the `CALL rel32`, and the second `CALL [rip]`), and all five
// resolve in BOTH bodies to the same absolute targets the J1 note above pins:
// 0x1801fc718, 0x18014b390, 0x1801fc718, 0x1800c6c00, 0x18014b388. Outside
// those five windows the three bodies are byte-for-byte equal.
J1_ENTRY(180037310)   // UI:          ComboElement::_sprites  (+0xf8)
J1_ENTRY(180024aa0)   // UI:          ComboElement::_userData (+0x108)

// WAVE 9 LANE UIC, 2026-09-08 — THE THIRTEENTH AND FOURTEENTH. Lane UIF's note
// above says "there are twelve"; there are at least fourteen, and its own
// mechanism explains why: a J1 address is invisible to the survey until
// something in our tree calls it. These two surfaced from the OTHER direction —
// from reading `?AddController@DialogInstance@UI@@` (0x180113470), which the
// integrator's unregistered-address-door census flagged, and where they appear
// as two of the five "unexported internals" WAVE9_LANE_UIF.md row 10 declined
// `?CreateInstance@ElementDescription@` on. They are not opaque internals. They
// are this.
//
// SAME BODY, established the same way and with the same numbers:
// tools/uic_bytediff.py compares all 115 bytes against 0x180033960 and resolves
// every differing byte back to the absolute operand it encodes.
//   0x180009460 — 10 differences, at offsets 40,41 61,62 72,73 77,78 87,88
//   0x180039440 — 11 differences, at offsets 39,40 60,61 71,72 76,77,78 86,87
//                 (byte-for-byte the same diff-set lane UIF reports for
//                  0x180037240, which is corroboration that the reference and
//                  the method are both unchanged)
// ZERO differences outside a displacement in either, and all five relocatable
// operands resolve to the same four absolutes (0x1801fc718 twice, 0x18014b390,
// 0x1800c6c00, 0x18014b388).
//
// A NOTE ON THE MEASUREMENT ITSELF, because it nearly went the other way. The
// first version of uic_bytediff.py carried a HARDCODED offset table for those
// five fields, decoded four bytes one position off for one of them, and printed
// "*** MISMATCH *** -> 0x1917d29b4" — a confident, specific, wrong refutation,
// for the REFERENCE body, which is registered and known good. Locating the
// fields by instruction pattern instead is what produced the numbers above.
// (reference_recheck_must_match_its_own_proof.md: a check whose own decode is
// wrong reads as a finding about the subject.)
J1_ENTRY(180039440)   // UI:          reached from ?AddController@DialogInstance@ 0x180113649
// VTABLE BATCH 2 (2026-09-12) - the two canonical free-and-zero ~Array bodies
// the UI::*Description second half needs. Each read from its own
// disassembly, not assumed from the element type: both free _data, zero
// {data, size+cap} and visit no element.
J1_ENTRY(18004cc90)   // UI:          UI::TabDescription's tab array
J1_ENTRY(180079870)   // UI:          UI::TableDescription's cell array
// WAVE 9 LANE PKG2 rung 4b-iii — the two array dtors ?~CompressedStream@IO@@
// (0x1800aa650) calls on its own members, at +0x40 and +0x30. NOT byte-identical
// to the already-ported 0x180024aa0 it calls at +0x50 (first difference at byte
// 39 of 256 for both), so they get their own entries rather than a shared
// registration — the same discipline the SetSize family forced twice this rung.
// ~Array for a trivially-destructible T frees _data and zeroes the header, so
// the element size that differs between them does not reach the body.
J1_ENTRY(1800249c0)   // IO:          CompressedStream::_headers (+0x40), Array<Header>
J1_ENTRY(1800248e0)   // IO:          CompressedStream::_windows (+0x30), Array<Window>

// ---- WAVE 25 LANE INTERNAL42: the eighteenth and nineteenth ----------------
// Both proved by an ADDRESS-NORMALISED identity check against the canonical
// body at 0x180004890, not by eye and not by a raw byte diff:
//
//     tools-side probe: read 0x73 bytes at each address, diff against the
//     canonical, and require (a) every differing byte to fall inside one of the
//     five 4-byte displacement fields and (b) all five RESOLVED absolute
//     targets to be equal. Raw diffs were 15/115 and 13/115 bytes — i.e. a raw
//     byte diff of two copies of ONE function says "13% different", because
//     every rel32/RIP field encodes a DISTANCE
//     (reference_position_only_diffs_hide_one_body.md).
//
//     Resolved targets, identical in all three:
//         0x1801fc718 (allocator singleton)  0x18014b390 (EnterCriticalSection)
//         0x1800c6c00 (Free)                 0x18014b388 (LeaveCriticalSection)
//
//     NEGATIVE CONTROL, in-population: the J2 body 0x18002ac40 — same family,
//     one extra element-destroy CALL at the top — pushed through the SAME check
//     reports 63 differing bytes OUTSIDE the displacement fields and unequal
//     targets. Without it, "IDENTICAL" would have been a screen that passes
//     everything in this neighbourhood.
// Cutover closure batch 2 (2026-09-12): 0x180051ef0 has no element-destroy
// CALL at all — just the allocator Enter/Free/Leave triple and the two zero
// stores. Same J1 body, no new spec.
// ---- CUTOVER CLOSURE BATCH 2b (2026-09-12): nine more J1 addresses ---------
//
// Every one was decompiled individually and is the SAME 115-byte body lane Q
// proved twelve times — `free(_data); _data = 0; {_size,_capacity} = 0` — with
// no element-destroy prologue at all. J1 has no parameters (not even a stride),
// so nine registrations cost nothing but nine lines, and each still owns its
// own Site so a run can say which address it reached.
J1_ENTRY(180014d00)   // Graphics: 0x180014c70's operator= clears through this
J1_ENTRY(180062320)   // Graphics
// ---- CUTOVER VTABLE BATCH 4 "elem_B", 2026-09-12 --------------------------
// Two more copies of the parameterless `~Array` body, both surfaced the usual
// way: nothing in our tree called them until a destructor that does was
// ported (reference_porting_can_grow_the_cutover_surface.md).
//   0x180043cb0 — the array `??1LayoutElement@UI@@` destroys at this+0xC0
//                 (the per-child layout records, stride 0x44)
//   0x1800490e0 — the array `??1RadioElement@UI@@`  destroys at this+0xC0
//                 (RadioElement::_checkBoxes, Array<Element*>)
// SAME BODY, and that is an instruction-level claim, not an eyeball one: both
// were disassembled in full against the canonical 0x180033960 — 29
// instructions each, 29 identical mnemonics and operands in the same order,
// and all four external operands resolve to the SAME absolutes the J1 note
// above pins (0x1801fc718 twice, 0x18014b390, 0x1800c6c00, 0x18014b388). The
// only textual differences are the addresses of the instructions themselves.
J1_ENTRY(180043cb0)   // UI:          LayoutElement::_rows       (+0xC0)


#undef J1_ENTRY

// 0x18001cdf0 — `Array<int32>::operator=`. Body and evidence above.
void* RtColl_I32ArrayAssign_18001cdf0(Collection::Array<int32_t>* dst,
                                      const Collection::Array<int32_t>* src) {
    void* r = RtColl_I32ArrayAssign(dst, src);
    return r;
}


// --- J3: the two-argument form, `SetSize(this, int n)` ----------------------
#define J3_ENTRY2(ADDR, SPEC)                                                 \
    void RtColl_ArraySetSize_##ADDR(void* a, int n) {                         \
        AsArray<&SPEC>(a)->SetSize(n);                                       \
    }

J3_ENTRY2(1800604e0, kSpec_PtrZero)     // Graphics:    Material::_programs, Array<ExternalPtr<VideoProgram>> (2026-09-12)
J3_ENTRY2(180056240, kSpec_VertexElement) // Video:      VertexLayout::_elements (2026-09-12)
J3_ENTRY2(18002fea0, kSpec_I32)         // Object/UI:   Array<int32>, tail UNINITIALISED
J3_ENTRY2(180059140, kSpec_Transform)   // Picking:     Array<Transform>, 4x unrolled in retail
J3_ENTRY2(1800764c0, kSpec_PtrZero)     // UI:          Array<InternalPtr<Element>>
// WAVE 10 LANE UIORACLE — TextElement::_parameters (+0xc8) [NON-TRIVIAL]
J3_ENTRY2(180029a80, kSpec_StringRefZ)  // UI:          Array<System::String>, tail ZEROED
// WAVE 9 LANE PKG2 rung 4b, 2026-09-08 — two more two-argument instantiations,
// both reached from IO::ReadPackfile (the packfile entry directory). The
// integrator's guess that these would be a REGISTRATION rather than a port was
// half right: they belong to this family, but neither is byte-identical to any
// member already here, so each needs its own entry. The spec is read out of the
// body, not inferred from the caller's element type:
//   0x180029510  SHL RCX,0x2 / MOV EDX,0x4 / `MOV EAX,[RSI+RCX*4]; MOV [RDX],EAX`
//                and the copy loop JGEs past the tail, so the tail is left
//                UNINITIALISED: kSpec_I32, same as 0x18002fea0.
//   0x180029630  NO shift at all (element size 1) / MOV EDX,0x1 / `MOVZX EAX,
//                byte [RCX+RDI]; MOV [RDX],AL`, tail likewise UNINITIALISED:
//                kSpec_Bool, same as 0x180008b90.
// Byte-similarity agrees with both readings (first difference at 93 and 89
// respectively, i.e. a shared ~90-byte prologue then the element op) — but the
// similarity is the CORROBORATION, not the evidence.
J3_ENTRY2(180029510, kSpec_I32)         // IO:          Array<int32>, tail UNINITIALISED
J3_ENTRY2(180029630, kSpec_Bool)        // IO:          Array<bool>,  tail UNINITIALISED
// Rung 4b-ii: the packfile block directory. 8-byte elements, tail ZEROED
// (`MOV [RDX],R10` with R10 == 0), but align 4 — see kSpec_U64ZeroA4 above.
J3_ENTRY2(180024d70, kSpec_U64ZeroA4)   // IO:          Array<CompressedStream::Header>
J3_ENTRY2(180024c10, kSpec_Window)      // IO:          Array<CompressedStream::Window>
// ---- WAVE 25 LANE INTERNAL42: Wave 24 lane INTERNAL8's declared debt --------
// Both of these became census rows because INTERNAL8's faithful port of the
// Persistence worker reached them by address; that lane declared the +2 rather
// than hiding it, and named its addressee. This is that addressee.
//
// Each spec is read out of ITS OWN body, and the two differ in every field that
// matters, so neither could have been inferred from the other:
//   0x18002a9d0  SHL RCX,0x3 / MOV EDX,0x8, copy is one qword move, and the
//                copy loop's else arm is empty -> stride 8, align 8, tail
//                UNINITIALISED. That is kSpec_Fill8 exactly, so it is REUSED
//                rather than duplicated. NOT kSpec_PtrZero: this body has no
//                zero-first store and no init arm.
//   0x18002a810  LEA RCX,[RAX+RAX*2] / SHL RCX,0x2 / MOV EDX,0x4, copy is three
//                dword moves -> stride 0xc, align 4, tail UNINITIALISED. No
//                existing row has stride 0xc; see kSpec_Blk12 above.
// ---- WAVE 25 LANE INTERNAL42: the two UI ExternalPtr arrays ----------------
// Wave 23 recorded 0x18002f070 and 0x1800492d0 as byte-identical (address-
// normalised hash 4645ec). Unlike the J2 trio in array_destroy.cpp, that
// identity IS load-bearing HERE, and the difference is worth stating because it
// is the same hash property read two ways: a SetSize body's stride, alignment
// and element ops are IMMEDIATES and opcodes, which an address-normalised hash
// preserves; a J2 body's spec lives in the rel32 of a CALL, which it erases. So
// the hash binds these two and not those three.
// Re-derived from the bodies anyway rather than taken from the hash:
//   SHL RCX,0x3 / MOV EDX,0x8                -> stride 8, align 8
//   MOV RAX,[RSI+RCX*8] / MOV [RDX],RAX      -> a PLAIN qword copy, so Assign8
//                                               and NOT Assign8Z: there is no
//                                               zero-first store in either body
//   MOV [RDX],R10 with R10 = 0               -> the new tail IS ZEROED
// which is kSpec_U64Zero exactly. Reused, not duplicated.
J3_ENTRY2(18002f070, kSpec_U64Zero)     // UI:          Array<ExternalPtr<External>>
J3_ENTRY2(1800492d0, kSpec_U64Zero)     // UI:          Array<ExternalPtr<ElementDescription>>
// 0x180005110 — Array<ExternalPtr<Goal>>, a door Wave 23's Achievement lane
// opened. Same three reads off its own body (SHL 3 / MOV EDX,8 / plain qword
// copy / `MOV [RDX],R10` with R10 zero), so the same row. Named here as an
// ExternalPtr array and given kSpec_U64Zero rather than kSpec_PtrZero, because
// the ELEMENT TYPE does not decide the spec: 0x180008d60's ExternalPtr array
// really does store a zero first and this one does not, and only the body says
// which.

// WAVE 26 LANE INT33 — see kSpec_Sprite38Z for why this is NOT kSpec_Sprite38
// even though the two bodies agree on stride, align, assign helper and destroy
// helper. [NON-TRIVIAL], but registered UNCONDITIONALLY; see the registration.
// WAVE 27 LANE FRONTIER - the cutover frontier. Two-argument, read from the
// body (see kSpec_ExtArrayElem): nothing homes or reads R8.
// WAVE 27 LANE FRONTIER - the array ?Serialize@StringTable@UI@@ resizes on the
// read path. Two-argument, read from the body (see kSpec_StringPair10).
// Cutover closure batch 2 (2026-09-12): Graphics::Material::_parameters,
// ~MaterialInstance's array's sibling SetSize.
J3_ENTRY2(1800616c0, kSpec_MaterialParam) // Graphics: Material::_parameters
// 2026-09-12 — the two Video::ShaderProgram instantiations. Both two-argument,
// read from their own bodies; see the two spec rows above.
J3_ENTRY2(180055db0, kSpec_ShaderParam)  // Video: ShaderProgram::_textures / _constants [NON-TRIVIAL]
J3_ENTRY2(180053f00, kSpec_InnerArrayU8) // Video: ShaderProgram::_byteCode            [NON-TRIVIAL]

// UI leftovers batch (docs/history/runtime_port/closure/WORKLIST.md) — the two-argument
// CaptureEntry and Sprite instantiations SpriteContext::Begin (0x18011d2b0)
// calls to clear SpriteCapture's `_entries` and `_sprites` to size 0.
J3_ENTRY2(18004ba90, kSpec_CaptureEntryZ) // UI: SpriteCapture::_entries clear
J3_ENTRY2(1800370c0, kSpec_SpriteIndexZ)  // UI: SpriteCapture::_sprites clear
// ---- CUTOVER CLOSURE BATCH 2b (2026-09-12) --------------------------------
J3_ENTRY2(18005cc60, kSpec_Ptr8Zero)    // Graphics:    Array<T*>, plain copy, zero tail
J3_ENTRY2(1800136f0, kSpec_Tri18A4)     // Graphics:    EdgeMesh's TriangleList (align 4)
J3_ENTRY2(180061880, kSpec_StringExtZ)  // Graphics:    Material's {String*,ExternalPtr}, zero tail
// ---- VTABLE BATCH 2 (2026-09-12) -----------------------------------------
J3_ENTRY2(18004cdf0, kSpec_ExtPtrPair10) // UI: TabDescription's tab array
J3_ENTRY2(1800799a0, kSpec_TableCell10)  // UI: TableDescription's cell array
J3_ENTRY2(180043d80, kSpec_LayoutEntry38) // UI: LayoutDescription's entry array [NON-TRIVIAL]


// ---- VTABLE BATCH 7b (2026-09-12) -----------------------------------------
// Callers: src/engine/graphics/vtbatch7_animgroup_particlesystem.cpp
// (AnimationGroup::Serialize and ParticleSystem::Serialize, both already ours).
// Each was reached BY ADDRESS before this commit and fell through to the DLL;
// registering them closes six cutover doors and changes nothing while the DLL
// is present, because ResolveAddr consults the port registry first either way.
J3_ENTRY2(180059e70, kSpec_AnimClip70)   // Graphics: AnimationGroup::_clips  [NON-TRIVIAL]
J3_ENTRY2(18005a220, kSpec_TagStrStrF18) // Graphics: AnimationGroup::_tags   [NON-TRIVIAL]
J3_ENTRY2(18005a410, kSpec_I32)          // Graphics: AnimationGroup::_floats (Array<float>, tail UNINITIALISED)
J3_ENTRY2(180059930, kSpec_IdExtPtr10)   // Graphics: AnimationGroup::Clip::_events
J3_ENTRY2(180021220, kSpec_Fill8)        // Graphics: ParticleSystem pool pointer array
J3_ENTRY2(180020e90, kSpec_Rec14)        // Graphics: one pool record's inner array

#undef J3_ENTRY2

// --- J3: the three-argument form, `SetSize(this, int n, const T* fill)` -----
#define J3_ENTRY3(ADDR, SPEC)                                                 \
    void RtColl_ArraySetSizeFill_##ADDR(void* a, int n, const void* fill) {   \
        AsArray<&SPEC>(a)->SetSizeFill(n, fill);                             \
    }

J3_ENTRY3(180023830, kSpec_Fill4)       // Graphics: Array<uint32>
J3_ENTRY3(180043b40, kSpec_LayoutInfo44) // UI: LayoutElement's per-cell info array (batch 2)
// WAVE 29 LANE GFXBODY — Array<RendererDrawable>::SetSize, the append behind
// ?AddLight@Renderer@Graphics@@ (0x1801051d0). kSpec_Fill16 is not inferred
// from the element type: 0x180063a80 was compared against the known Fill16 site
// 0x18004f790 under POSITION-INDEPENDENT normalisation (in-function branch
// targets relative, external call and RIP targets ABSOLUTE — a raw byte diff of
// two copies of one function reports ~23% different because rel32 encodes a
// distance) and came out 79 of 79 instructions IDENTICAL, with a positive
// control (a function against itself, 79/79) and a negative control (an
// unrelated function, 0/79) run first. Both are 262-byte flags-0x2 .pdata rows.
// Registering it matters at cutover: unregistered, 0x180063a80 resolves to
// nothing and AddLight silently fails to append the light.
J3_ENTRY3(180063a80, kSpec_Fill16)      // Graphics: Renderer::_drawables push (AddLight)
J3_ENTRY3(180076900, kSpec_Fill8Z)      // UI:       Element::AddChild's child array
J3_ENTRY3(18007a0d0, kSpec_Fill8Z)      // Engine:   the dialog pending-list push
// WAVE 9 LANE UIW, 2026-09-08 — five more three-argument instantiations, all
// reached from the UI cutover residue. THREE OF THE FIVE ARE EXACT CLONES of
// specs already above, and that is a claim read out of each body, not assumed
// from the element type:
//   0x180024fe0  ComboElement::_userData  — SHL RCX,3 / MOV EDX,8 / plain
//                `MOV RAX,[src]; MOV [dst],RAX`. No zero-first store, so
//                Assign8, not Assign8Z: kSpec_Fill8, same as 0x18001f990.
//   0x18004bfc0  ComboElement::_sprites   — SHL RCX,4 / MOV EDX,8 / two qword
//                copies. kSpec_Fill16, same as MainLoop's two push sites.
//   0x180048fe0  RadioElement::_checkBoxes — SHL RCX,3 / MOV EDX,8 / `MOV
//                [dst],R11(=0)` THEN `MOV RAX,[src]; MOV [dst],RAX`. The
//                zero-first store is the smart-pointer element's operator=,
//                so kSpec_Fill8Z, same as Element::AddChild's own array.
// The other two carry the new specs above.
J3_ENTRY3(180024fe0, kSpec_Fill8)       // UI:       ComboElement::_userData  (+0x108)
J3_ENTRY3(18004bfc0, kSpec_Fill16)      // UI:       ComboElement::_sprites   (+0xf8)
J3_ENTRY3(1800298f0, kSpec_PkgEntry)    // IO:       PackageFile's entry array [NON-TRIVIAL]
J3_ENTRY3(180034860, kSpec_StringRef)   // UI:       ComboElement::_strings   (+0xe8) [NON-TRIVIAL]
// WAVE 9 LANE GFX3 — the Graphics::Scene node array. Registered HERE and not in
// src/engine/graphics/scene_grid.cpp on purpose: it is a member of this
// family, and a private second copy of SetSizeCore in the graphics tree is
// exactly the drift this parameterisation exists to prevent. Its oracle is the
// graphics grid probe, whose append arm drives it against the shipped body.
// WAVE 9 LANE UIF — the UI sprite batch. Registered HERE for the same reason
// lane GFX3 gave for 0x18005fb00: it is a member of this family, and a private
// second copy of SetSizeCore in the UI tree is the drift this parameterisation
// exists to prevent. Its only caller is SpriteContext::AddSpriteDraw
// (0x18011c890, ported in src/engine/ui/spritecontext.cpp), which appends one
// element by passing `size + 1` and a pointer to the record.
// WAVE 10 LANE UI3 — the two grow helpers the four allocating DialogInstance
// bodies tail-call (src/engine/ui/dialog_create.cpp). Registered HERE, beside
// the specs they reuse, for exactly the reason lane GFX3 and lane UIF each gave
// for theirs: a private second copy of SetSizeCore in the UI tree is the drift
// this parameterisation exists to prevent.
//
// NEITHER IS NEW CODE. Each is BYTE-IDENTICAL to a body this file already
// registers, and that is a MEASUREMENT: both listings were compared
// instruction-for-instruction with the leading address stripped and every
// INTERNAL branch target rewritten as a body-relative delta, so every operand
// naming anything outside the body had to match ABSOLUTELY
// (reference_position_only_diffs_hide_one_body.md).
//
//   0x180076250 vs 0x18005fb00 (kSpec_Node18, stride 0x18)  -> 0 differences
//   0x180076370 vs 0x180076900 (kSpec_Fill8Z, stride 8)     -> 0 differences
//
// and all five relocatable operands in each resolve to the same absolutes
// (0x1800c7a50, 0x1801fc718, 0x18014b390, 0x1800c6c00, 0x18014b388).
//
// THE INSTRUMENT WAS CONTROLLED. The same comparison run between the stride-8
// body and the stride-0x18 body reports 62 differing instructions, so it is not
// structurally incapable of saying "different"
// (reference_diagnostic_field_cannot_express_the_answer.md). The two listings,
// the script and both runs are in docs/history/runtime_port/WAVE10_LANE_UI3.md §2.

// UI leftovers batch — the three-argument CaptureEntry and Event pushes:
// SpriteContext::Begin's own callees build these with a fill value.
J3_ENTRY3(180033690, kSpec_Event30)      // UI: UserInterface::_events append
// ---- CUTOVER CLOSURE BATCH 2b (2026-09-12) --------------------------------


#undef J3_ENTRY3

// --- J3: the constant-folded one-argument form ------------------------------
// 0x18007b960 is `Array<int>::SetSize(10000)` with the fill value -1, and BOTH
// are immediates in the shipped body: `CMP ESI,0x2710`, `MOV [RCX+8],0x2710`,
// `MOV [RCX+0xc],0x7530` (the folded 3 * 10000) and `MOV [RDX],0xffffffff`.
// It takes ONE argument. Registering it behind the two- or three-argument
// signature would read a register the caller never set.

// WAVE 9 LANE UIF — 0x1800783d0 is `Array<UI::SpriteDraw>::SetSize(0)`, the
// SAME one-argument shape as 0x18007b960 above with the new size folded to the
// constant 0. It is NOT a member of the J1 `~Array` family, and reading it as
// one would be a live defect rather than a harmless mislabel — so the
// distinction is spelled out here rather than left to the name:
//
//   ~Array (J1) FREES the buffer unconditionally. This body does not free at
//   all. Its epilogue is `CMP RSI,[RBX] / JZ return` at 0x1800784a1, where RSI
//   is the data pointer captured on entry and [RBX] the current one. Nothing
//   between them can reallocate when the new size is 0 (the grow branch is
//   `CMP [RCX+0xc],0 / JGE skip`, and capacity is never negative), so the two
//   are always equal and the MemFree at 0x1800784d2 is UNREACHABLE. The
//   element loop at 0x180078441 is unreachable for the same reason — its guard
//   is `TEST EAX,EAX / JNS`, and EAX is the old size, which is > 0 on every
//   path that gets there.
//
//   So the whole observable effect is `_size = 0`, capacity and buffer kept.
//   That is exactly what SpriteContext::FlushDeferred (0x18011c6a0) wants: it
//   drains the deferred batch every scissor change and would thrash the heap
//   if this freed. Registering the J1 body here instead would free a buffer
//   retail keeps and hand the next AddSpriteDraw a dangling pointer.
//
// SetSizeCore is still the right implementation, not a hand-written `_size=0`:
// it reproduces the same unreachable branches for the same reasons, so if the
// reasoning above is ever wrong the two disagree under the diff oracle instead
// of agreeing by construction.
void RtColl_ArraySetSize_1800783d0(void* a) {
    AsArray<&kSpec_SpriteDraw>(a)->SetSize(0);
}

// ============================================================================
// CUTOVER CLOSURE BATCH 2b (2026-09-12) — `Reserve`, the fourth Array job
// ============================================================================
//
// 0x18002fd90 (`Collection_Array_InternalPtr_Reserve`, already carrying that
// recovered name in Ghidra) is NOT a `SetSize`, and running it through
// SetSizeCore would be a live defect rather than a mislabel. Read out of its
// own body, the differences from J3 are all in the tail:
//
//   * It is a GROW-ONLY no-op guard: `if (newSize <= _size) return;` — J3's
//     guard is `newSize == _size`, so J3 would SHRINK on a smaller argument
//     where Reserve does nothing.
//   * It RESTORES `_size` to the old value at the end (`MOV [RCX+8],<old>` at
//     the epilogue). The whole point is capacity, not length.
//   * Its copy loop runs ONLY when the buffer actually moved (`CMP <old>,[RCX]`
//     / `JZ`), and it copies `oldSize` elements — not `newSize`. J3 copies the
//     prefix on the no-realloc path too (that path is a self-copy and so is
//     invisible, which is exactly why the difference is easy to miss).
//   * The `MemFree(old)` is likewise inside the moved-buffer branch, and is
//     itself null-guarded before the Enter/Free/Leave triple.
//
// Element operations are still an ArraySpec: stride 8, align 8, a PLAIN qword
// move (no zero-first store — checked against Assign8Z's shape), which is
// `kSpec_Fill8` exactly. So the spec framework is reused and only the control
// flow is new. One address today; the job is written parameterised because the
// family has never had exactly one instantiation of anything.
void ReserveCore(ArrayHdr* a, int newSize, const ArraySpec* spec) {
    const uint32_t stride = spec->stride;

    const int32_t oldSizeI = a->_size;
    if (oldSizeI >= newSize) {
        return;
    }

    uint8_t* const old = a->_data;
    a->_size = newSize;                      // set, then restored below

    if (newSize > a->_capacity) {
        a->_capacity = newSize;
        if (newSize == static_cast<int32_t>(oldSizeI + 1) && oldSizeI != 0)
            a->_capacity = static_cast<int32_t>(newSize * 3);
        a->_data = static_cast<uint8_t*>(
            MemAlloc(static_cast<uint64_t>(static_cast<int64_t>(a->_capacity)) * stride,
                     spec->align));
    }

    if (old != a->_data) {
        for (int64_t i = 0; i < static_cast<int64_t>(oldSizeI); ++i) {
            uint8_t* dst = a->_data + i * stride;
            if (dst == nullptr) continue;    // retail tests the COMPUTED address
            spec->assign(dst, old + i * stride);
        }
        MemFree(old);                        // null-guarded inside 0x1800c7ad0
    }

    a->_size = oldSizeI;                     // *** the whole point of Reserve ***

}


// CUTOVER CLOSURE BATCH 2b — 0x180051020, the THIRD constant-folded one-argument
// SetSize (after 0x18007b960 and 0x1800783d0). `Audio::Scene`'s 256-channel
// table: `CMP E..,0x100` / `MOV [RCX+8],0x100` / `MOV [RCX+0xc],0x300`, and the
// loop bound `CMP R..,0x7000` is 256 * 0x70 written as a byte offset. It takes
// ONE argument; a two-argument registration would read a register the caller
// never set.


// ============================================================================
// CUTOVER CLOSURE BATCH 2b (2026-09-12) — `Remove(const T&)`, the fifth job
// ============================================================================
//
// 0x180050e00, reached from `RemoveEmitter` (0x1800decf0) in Audio. A
// search-and-erase over a 16-byte element whose first three dwords are compared
// with FLOATING-POINT equality (UCOMISS, not CMP) — a Vector3 key in a padded
// 16-byte slot. It removes EVERY match, not the first, and it compacts in place
// with no reallocation and no capacity change.
//
// Transcribed from the body rather than rewritten as an idiomatic erase-remove,
// because three details are load-bearing and all three would be "tidied" away:
//
//   * The shift loop re-reads `this->_size` on EVERY iteration (`CMP <k>,
//     [RBX+8]`), and the outer loop re-reads it again after each removal. The
//     element move is guarded by `k < _size` — so on the LAST element the guard
//     fails and the move is skipped, which is what makes the walk terminate.
//   * The compare is float `==`. A NaN key matches nothing, including a stored
//     NaN. `memcmp` would match it, and -0.0f/+0.0f would stop matching.
//   * The element move is FOUR DWORDS (+0x00..+0x0f) from index k+1 to k, and
//     the address `base + o2` is null-tested first — retail tests the COMPUTED
//     address, as everywhere else in this family.
//
// No ArraySpec: the element operations are inlined constants in this body (a
// three-float compare and a four-dword move), not a parameter, so a spec row
// would be a table with one entry and one user.

// UI leftovers batch — the SAME one-argument `SetSize(0)` shape as
// 0x1800783d0 above, for two more callers: `UserInterface::CleanRemovedDialogs`
// (0x18011f790) clears `_removedDialogs` (Array<DialogInstancePtr>, stride 8)
// and `UserInterface::UpdateEvents` (0x18011fc60) clears `_events`
// (Array<Event>, stride 0x30). Both bodies were read out of the disassembly
// and both have the identical dead-branch shape 0x1800783d0's banner already
// explains: with `n` folded to the literal 0, the fill loop's guard is
// `start(=oldSize) < n(=0)`, which is false for every non-negative oldSize, so
// the loop NEVER RUNS and the realloc/free branches are equally unreachable
// (capacity is never negative, so `pvVar2 == *param_1` is always true). The
// only observable effect of either body is `_size = 0` — no element is
// destroyed, no buffer is freed, no per-element write happens. That matters
// here specifically: `_removedDialogs` holds DialogInstance pointers already
// released by the vtable-slot-0 call CleanRemovedDialogs makes just before
// this clear, and `_events` holds no owned resource (kSpec_Event30's banner),
// so a `_size = 0` that keeps the buffer is exactly what retail does and not
// a hand-written shortcut around it.
//
// SetSizeCore is still the right implementation, for the same reason
// 0x1800783d0 gives: if the "always dead" reasoning is ever wrong for either
// spec, the two disagree under the diff oracle instead of silently agreeing.


// ---- BATCH 4 (Graphics render/cull/sun-camera cluster) ---------------------
// Two more one-argument constant-folded members, both reached from that
// cluster. Each spec is read out of ITS OWN body; neither could have been
// inferred from the caller's element type.
//
// 0x18000c300 — `SetSize(4)` over a 0x8ba0-byte element (Engine::Play's
// per-something block array). See kSpec_Blob8ba0 above for the three reads.
// The grow rule folds the same way every other member does, which is how the
// generic core is known to fit: retail's `_capacity = 4; if (oldSize == 3)
// _capacity = 12;` is `(newSize == oldSize + 1 && oldSize != 0) ? n*3 : n`
// with n folded to 4.
void RtColl_ArraySetSize_18000c300(void* a) {
    AsArray<&kSpec_Blob8ba0>(a)->SetSize(4);
}

// 0x18005f980 — NOT a SetSize at all but a `Reserve(16384)`: SetSize(0x4000)
// with the OLD size written straight back over it at the end
// (`MOV [RCX+8],ESI` at the epilogue, where ESI is the size captured on
// entry). The one caller is Graphics::Scene::AddCulledDrawItems (0x1801017a0),
// which reserves room for the visible-object list up front so its inlined
// per-object append never reallocates.
//
// *** REGISTERING THE PLAIN kSpec_Fill8 SetSize BODY HERE WOULD BE A DEFECT,
// not a rounding error: it would leave `_size` at 16384 and the caller would
// then dispatch Draw through 16384 uninitialised pointers. ***
// The restore is therefore part of the port, outside SetSizeCore.
//
// Spec read off the body: `MemAlloc(capacity << 3, 8)` -> stride 8, align 8;
// the copy is one qword move; the copy loop is bounded by the OLD size, so the
// new tail is left UNINITIALISED. That is kSpec_Fill8 exactly — and it does not
// matter that the tail is never initialised, because `_size` goes back to the
// old value before anything can look at it.
//
// Retail also has no `newSize == oldSize` early return here (it was folded
// away). SetSizeCore keeps one, and it is unobservable: the only state that
// path would skip is `_size = 0x4000`, which the restore below undoes.
void RtColl_ArrayReserve_18005f980(void* a) {
    ArrayHdr* h = static_cast<ArrayHdr*>(a);  // CAST-OK: elem_stride
    const int32_t oldSize = h->_size;
    SetSizeCore(h, 0x4000, nullptr, &kSpec_Fill8);
    h->_size = oldSize;
}

// ============================================================================
// BATCH 5 (2026-09-12) — three more SetSize instantiations, two element types
//
//   0x18002ae20  Array<IO::Persistence::SaveGameInfo>::SetSize(n, fill)
//   0x18002aae0  the SAME array's SetSize(0), constant-folded to one argument
//   0x180028cd0  Array<{uint32 typeId, Object::External*, System::Filename}>
//                ::SetSize(n, fill) — IO::Modification::SetupFileSystem's
//                reload list
//
// NEITHER ELEMENT TYPE MATCHES AN EXISTING ROW, and that was MEASURED before
// either spec was written rather than assumed from the address neighbourhood:
// each body was byte-compared against the twelve non-trivial rows already in
// this file and the closest match differed in 187 of 333 / 242 of 364 bytes.
// The specs below are read out of each body's own disassembly.
//
//   0x18002ae20   SHL RCX,0x4        -> stride 0x10
//                 MOV EDX,0x8        -> align 8
//                 MOV RAX,[RDX] / MOV [RCX],RAX            +0x00 verbatim
//                 MOV [RCX+8],0 / TEST / store / INC [RAX-4]
//                                                          +0x08 String adopt
//                 == AssignExtStringAt08 exactly, the helper 0x180075d20's row
//                 already uses. NO INIT ARM: the copy loop's source is a
//                 `CMOVL` between the old element and the fill and there is no
//                 third branch, so this address is only ever the 3-arg form.
//                 destroy CALL 0x18002afc0 on both arms (kD_Str_10at08).
//
//   0x180028cd0   LEA RCX,[RAX+RAX*2] / SHL RCX,0x3 @0x180028d2d -> stride 0x18
//                 MOV EDX,0x8        @0x180028d35             -> align 8
//                 MOV EAX,[RDX] / MOV [RCX],EAX  @0x180028d8a  +0x00 DWORD
//                 MOV RAX,[RDX+8] / MOV [RCX+8],RAX           +0x08 qword
//                 ADD RCX,0x10 / ADD RDX,0x10 / CALL 0x1800067d0
//                                                             +0x10 Filename
//                 destroy CALL 0x180029190 @0x180028dcd (kD_Str_18).
//
//   *** 0x180028cd0's +0x04..+0x07 IS NEVER WRITTEN. *** The first store is a
//   DWORD and the next is at +0x08. An oracle that compared those four bytes
//   would fail 100% of calls and read like a catastrophic port defect
//   (reference_oracle_must_not_compare_unwritten_bytes.md). Declared in the
//   assign window below. There is no init arm, so there is no init window to
//   read separately — the extent belongs to the ARM, and this element type has
//   exactly one.
//
// SaveGameInfo's layout is corroborated by an INDEPENDENT witness:
// src/engine/io/Persistence.h has said since Wave 19 lane PERSIST that
// `?GetSaveInfo@Persistence@IO@@` indexes the array at +0x80 with `SHL 4`,
// i.e. a 0x10 stride, and the save-list job ported in this same commit fills
// {timestamp, name} in that order. Derived here from the RESIZER, there from
// the PRODUCER and the CONSUMER, with no shared premise.
// ============================================================================

// 0x180028cd0's element assign. The Filename copy constructor is reached BY
// ADDRESS, not as a direct call, for the reason `AssignGraphSeries` above
// records: retail's 0x180028d9e is a CALL to that address and the registry is
// the door every other caller of it uses (src/engine/io/modification.cpp and
// src/engine/collection/avl_trees.cpp both reach 0x1800067d0 the same way).
// The element is RtIO::ReloadEntry (types/io/Modification.h).
static void ReloadEntryAssign(RtIO::ReloadEntry* d, const RtIO::ReloadEntry* s) {
    d->typeId = s->typeId;
    /* +0x04..+0x07 deliberately not copied — retail's first store is a DWORD */
    d->external = s->external;

    new (&d->file) System::Filename(s->file);   // 0x1800067d0 (the slot is raw storage)
}
void AssignModExternal18(uint8_t* d, const uint8_t* s) {
    AssignAs<RtIO::ReloadEntry, &ReloadEntryAssign>(d, s);
}

// The two destroy ranges. Both are ported AND registered in array_destroy.cpp
// in this same commit (`B_ENTRY(18002afc0, kD_Str_10at08)` and
// `B_ENTRY(180029190, kD_Str_18)`), so these ResolveAddr calls are
// registry-first and land on OUR bodies rather than opening a door onto an
// unported callee — the failure mode
// reference_porting_can_grow_the_cutover_surface.md names.
void DestroySaveGameInfoRange(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_18002afc0(ignoredThis, base, count);
}
void DestroyModExternal18Range(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_180029190(ignoredThis, base, count);
}

// NOT kSpec_ExtStr10at08, and the difference is the two fields that decide
// behaviour: that row has `init = InitZero10` and destroys through
// 0x180039640. This one has NO init arm at all and destroys through
// 0x18002afc0. Reusing it would zero a tail retail never writes and free
// through the wrong instantiation's helper.
const ArraySpec kSpec_SaveGameInfo = {           // 0x18002ae20, 0x18002aae0
    "Array<IO::Persistence::SaveGameInfo>", 0x10, 8,
    &AssignSaveGameInfo, nullptr, &DestroySaveGameInfoRange, false,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0x08, 0x10 };   // only the String slot is dereferenced

const ArraySpec kSpec_ModExternal18 = {          // 0x180028cd0
    "Array<{uint32, External*, Filename}>", 0x18, 8,
    &AssignModExternal18, nullptr, &DestroyModExternal18Range, false,
    /*assign unwritten*/ 0x04, 0x08,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0x10, 0x18 };   // the Filename slot is dereferenced

#define J3_ENTRY3(ADDR, SPEC)                                                 \
    void RtColl_ArraySetSizeFill_##ADDR(void* a, int n, const void* fill) {   \
        AsArray<&SPEC>(a)->SetSizeFill(n, fill);                             \
    }

J3_ENTRY3(180028cd0, kSpec_ModExternal18)  // IO: SetupFileSystem's reload list

#undef J3_ENTRY3

// 0x18002aae0 — the SAME Array<SaveGameInfo> with the new size folded to the
// constant 0, exactly the one-argument shape 0x1800783d0 above has.
//
// IT IS NOT A MEMBER OF THE J1 `~Array` FAMILY and reading it as one would be
// a live defect: `~Array` frees the buffer unconditionally, and this body's
// grow test (`CMP dword [RCX+0xc],0 / JGE`, and capacity is never negative)
// can never be taken with a new size of 0, so its MemFree is unreachable and
// the buffer and capacity SURVIVE. What it DOES do — and what 0x1800783d0 does
// not — is run the shrink-path destroy over the old elements
// (`CALL 0x18002afc0` with count = the old size), which is how the save list
// releases its filename Strings before it is refilled.
//
// SetSizeCore is still the implementation rather than a hand-written
// `_size = 0`: it reproduces the same unreachable branches for the same
// reasons, so if that reading is ever wrong the two disagree under the oracle
// instead of agreeing by construction.


// ============================================================================
// BATCH 8b (2026-09-12) - 0x180052330, Audio::Scene::EffectSlot::positions
// ============================================================================
// `Collection::Array<Audio::EmitterInstance>::SetSize(int)`, the resize the
// EffectSlot inner-array serialize (0x180051e00, ported in
// src/engine/audio/audio_effectslot_serialize.cpp) calls on the READING side.
// Read out of the body at 0x180052330, not inferred from a sibling:
//
//   stride 0x10   SHL RCX,0x4                          at 0x180052391
//   align  4      MOV R14D,0x4 / MOV EDX,R14D          at 0x18005236e/0x180052395
//   assign        FOUR dword moves per element         at 0x180052411
//   init          the `else` arm writes TWO ZERO QWORDS at 0x180052433, so the
//                 whole stride is covered and there is no init window
//   destroy       NONE - the shrink path frees the old buffer and destroys
//                 nothing (0x180052557 goes straight to the locked free)
//
// It is NOT kSpec_Fill16A4 (0x180051aa0), which is the same stride and the same
// align 4 but has NO init arm; reusing it would leave a tail retail zeroes.
// It is NOT kSpec_Fill16 either, whose align is 8.
const ArraySpec kSpec_EmitterInstance = {        // 0x180052330
    "Array<Audio::EmitterInstance>", 0x10, 4, &Assign16, &InitZero10, nullptr,
    true, 0, 0, 0, 0, 0, 0 };



// ============================================================================
// CUTOVER CLOSURE BATCH D1 (2026-09-12) — the unnamed helper bodies below
// 0x1800571b0 that today's vtable ports reach by address.
// ============================================================================
//
// Every row below was read out of its OWN disassembly in retail
// Runtime-x64.dll. Where a row ends up parameter-identical to an existing
// kSpec_* that is SAID, and the row is reused only when stride, align, the
// element assign, the init arm AND the destroy callee all agree — the rule the
// kSpec_U64ZeroA4 / kSpec_Fill16A4 notes above exist for.
//
//   J1  (115 B, the parameterless `~Array`)  ten more copies
//   J3  thirteen SetSize instantiations, two of them constant-folded and two
//       three-argument
// The J2/J4/destroy halves of the same batch are in array_destroy.cpp.

// --- 0x18003eea0  Array<int32>, tail ZEROED ---------------------------------
// Stride 4 / align 4 (`SHL RCX,0x2` + `MOV EDX,0x4` at 0x18003eef3), assign is
// one dword move, and the else arm is `MOV dword [RDX],R10D` with R10D zeroed
// at 0x18003ef04 — so this is NOT kSpec_I32 (whose else arm is EMPTY) and not
// kSpec_Fill4 (no else arm either). One dword of difference, one new row.
void InitZero4(uint8_t* d) { InitAs<uint32_t, &ZeroWords<uint32_t, 1>>(d); }
const ArraySpec kSpec_I32Z = {                    // 0x18003eea0
    "Array<int32>(zero tail)", 4, 4, &Assign4, &InitZero4, nullptr, true,
    0, 0, 0, 0, 0, 0 };

// --- 0x18003df80 / 0x18003d1c0  Array<UI::DocumentText::DocumentItem> -------
// Stride 0x24 (`LEA RCX,[RAX+RAX*8]; SHL RCX,0x2` at 0x18003dfd3), align 4
// (`MOV EDX,0x4`), assign is NINE plain dword moves at +0x00..+0x20
// (0x18003e022..0x18003e055). No init arm at all in the three-argument body —
// its `else` takes the caller's `fill` through the same nine moves
// (`MOV RDX,R14 / CMP R9,RSI / CMOVL RDX,R10`), so `init` stays null and the
// one-argument sibling never reaches the loop anyway.
// NO DESTROY: a DocumentItem holds no refcounted field; both bodies free the
// old buffer and destroy nothing.
void Assign24d(uint8_t* d, const uint8_t* s) { AssignAs<uint32_t, &CopyWords<uint32_t, 9>>(d, s); }
const ArraySpec kSpec_DocItem24 = {               // 0x18003df80, 0x18003d1c0
    "Array<UI::DocumentText::DocumentItem>", 0x24, 4, &Assign24d,
    nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };

// --- 0x18003e1a0 / 0x18003d430  Array<UI::DocumentText::Link> ---------------
// The Link element is {System::String* @+0x00, int32 @+0x08, int32 @+0x0c,
// bool @+0x10} in a 0x18 slot; 0x18003c760 (the element's own `operator=`,
// ported in src/engine/ui/documenttext.cpp in this same commit) reads the same
// five fields and is the independent confirmation of the layout.
//   stride 0x18   LEA RCX,[RAX+RAX*2]; SHL RCX,0x3   0x18003e1f8
//   align  8      MOV EDX,0x8                        0x18003e200
//   assign        zero the slot, adopt the String (INC [str-4]), then the two
//                 int32 and the bool                 0x18003e257..0x18003e278
//   init          slot = 0; ONE QWORD of -1 over +0x08..+0x0f; bool = 0
//                 0x18003d4f7..0x18003d502 — i.e. both int32 become -1
//   destroy       0x18003e3e0 (Body B, `kD_Str_18at00`) on BOTH the realloc arm
//                 (0x18003e296) and the shrink-tail arm (0x18003e2e9)
// +0x11..+0x17 is written by NEITHER arm.
static void DocLinkAssign(UI::DocumentTextElement::Link* d,
                          const UI::DocumentTextElement::Link* s) {
    AdoptString(d->text._data, s->text._data);  // +0x00, zero-then-adopt
    d->firstItem = s->firstItem;
    d->lastItem  = s->lastItem;
    d->armed     = s->armed;
}
void AssignLink18(uint8_t* d, const uint8_t* s) {
    AssignAs<UI::DocumentTextElement::Link, &DocLinkAssign>(d, s);
}
static void DocLinkInit(UI::DocumentTextElement::Link* d) {
    d->text._data = nullptr;
    d->firstItem = -1;                      // ONE qword of -1 in retail (H7)
    d->lastItem  = -1;
    d->armed     = 0;
}
void InitLink18(uint8_t* d) { InitAs<UI::DocumentTextElement::Link, &DocLinkInit>(d); }
// By ADDRESS, registry-first: 0x18003e3e0 is registered by array_destroy.cpp in
// this same commit (`B_ENTRY(18003e3e0, kD_Str_18at00)`), so this lands on our
// body rather than opening a door onto an unported callee.
void DestroyLinkRange(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_18003e3e0(ignoredThis, base, count);
}
const ArraySpec kSpec_Link18 = {                  // 0x18003e1a0, 0x18003d430
    "Array<UI::DocumentText::Link>", 0x18, 8, &AssignLink18, &InitLink18,
    &DestroyLinkRange, false,
    /*assign unwritten*/ 0x11, 0x18,
    /*init   unwritten*/ 0x11, 0x18,
    /*seed zero       */ 0x00, 0x08 };   // the String slot is dereferenced

// --- 0x1800547e0  Array<System::String>, Video::PixelProgram's name table ---
// Parameter-identical to kSpec_StringRefZ (stride 8, align 8, AssignStringRef,
// InitZero8) IN EVERYTHING BUT THE DESTROY CALLEE: that row destroys through
// 0x180011010 and this body calls 0x180054920 (its own COMDAT copy of Body B,
// `kD_Str_08`). Both are the same body, but the spec names the ADDRESS our
// registry hands the call to, so this gets its own row rather than borrowing a
// sibling's door.
void DestroyStringRange_180054920(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_180054920(ignoredThis, base, count);
}
const ArraySpec kSpec_StringRefZ_PP = {           // 0x1800547e0
    "Array<System::String>(PixelProgram)", 8, 8, &AssignStringRef, &InitZero8,
    &DestroyStringRange_180054920, false,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0x00, 0x08 };

// --- 0x180054a20  Array<Video::PixelProgram::Sampler> -----------------------
// Element {System::String* @+0x00, int32 @+0x08, uint8 @+0x0c, uint8 @+0x0d,
// uint8 @+0x0e} in a 0x10 slot: +0x0f is written by NEITHER arm.
//   stride 0x10   SHL RCX,0x4        0x180054a71
//   align  8      MOV EDX,0x8        0x180054a75
//   assign        0x180054ac4..0x180054aef
//   init          ONE qword zero at +0x00 (0x180054b00) — +0x08..+0x0f stays
//   destroy       0x180054ba0 (Body B, `kD_Str_10`), both arms
static void PpSamplerAssign(Video::PixelProgram::Sampler* d, const Video::PixelProgram::Sampler* s) {
    StringAssign(&d->name._data, &s->name._data);            // +0x00
    d->unit  = s->unit;                                      // +0x08
    d->flagA = s->flagA;                                     // +0x0c
    d->flagB = s->flagB;                                     // +0x0d
    d->flagC = s->flagC;                                     // +0x0e
}
void AssignPpSampler10(uint8_t* d, const uint8_t* s) { AssignAs<Video::PixelProgram::Sampler, &PpSamplerAssign>(d, s); }
void DestroyStringRange_180054ba0(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_180054ba0(ignoredThis, base, count);
}
const ArraySpec kSpec_PpSampler10 = {             // 0x180054a20
    "Array<Video::PixelProgram::Sampler>", 0x10, 8, &AssignPpSampler10,
    &InitZero8, &DestroyStringRange_180054ba0, false,
    /*assign unwritten*/ 0x0f, 0x10,
    /*init   unwritten*/ 0x08, 0x10,
    /*seed zero       */ 0x00, 0x08 };

// --- 0x1800567d0  Array<Video::VertexProgram::Element> ----------------------
// Element {String* @+0x00, String* @+0x08, uint32 @+0x10, int32 @+0x14},
// stride 0x18 (`LEA RCX,[RAX+RAX*2]; SHL RCX,0x3`), align 8. TWO refcounted
// slots, which is why its destroy (0x180056960) is the new two-String Body B
// row `kD_Str2_18` in array_destroy.cpp and not `kD_Str_18at00`.
//   assign  0x180056878..0x1800568a8   two adopts then two dword moves
//   init    TWO qword zeros only (0x1800568b9/0x1800568bc) — +0x10..+0x17 is
//           left holding whatever the allocation contained
// B.4 (X-RL): the element is `Video::VertexProgram::Element` (ShaderProgram.h).
static void VtxElementAssign(Video::VertexProgram::Element* d,
                             const Video::VertexProgram::Element* s) {
    StringAssign(&d->name._data, &s->name._data);
    StringAssign(&d->semantic._data, &s->semantic._data);
    d->format = s->format;
    d->index  = s->index;
}
void AssignVtxElement18(uint8_t* d, const uint8_t* s) {
    AssignAs<Video::VertexProgram::Element, &VtxElementAssign>(d, s);
}
void InitZero16(uint8_t* d) { InitAs<uint64_t, &ZeroWords<uint64_t, 2>>(d); }
void DestroyVtxElementRange(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyStrings_180056960(ignoredThis, base, count);
}
const ArraySpec kSpec_VtxElement18 = {            // 0x1800567d0
    "Array<Video::VertexProgram::Element>", 0x18, 8, &AssignVtxElement18,
    &InitZero16, &DestroyVtxElementRange, false,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0x10, 0x18,
    /*seed zero       */ 0x00, 0x10 };   // both String slots are dereferenced

// --- 0x180056af0  Array<Video::VertexProgram::Stream> -----------------------
// Element {Collection::Array<Element> @+0x00 (the 0x10-byte header), String*
// @+0x10}, stride 0x18, align 8. The assign ZEROES the nested header and then
// runs the nested `operator=` (0x1800571b0) into it, then adopts the String.
// The destroy is 0x180056cb0 — String at +0x10 FIRST, then the nested array —
// and it runs on both the realloc and the shrink arm.
//
// 0x1800571b0 is reached by ADDRESS: it is the sibling batch's row, and
// registry-first means the day it is ported this call lands on that body with
// no edit here.
// B.4 (X-RL): the element is `Video::VertexProgram::Stream` (ShaderProgram.h).
static void VtxStreamAssign(Video::VertexProgram::Stream* d,
                            const Video::VertexProgram::Stream* s) {
    d->elements.data     = nullptr;                  // 180056bb3
    d->elements.size     = 0;                        // 180056bb6, one qword in retail (H7)
    d->elements.capacity = 0;
    RtColl_ArrayAssign_1800571b0(&d->elements, &s->elements);              // 180056bc5
    StringAssign(&d->name._data, &s->name._data);    // 180056bcd..180056bdd
}
void AssignVtxStream18(uint8_t* d, const uint8_t* s) {
    AssignAs<Video::VertexProgram::Stream, &VtxStreamAssign>(d, s);
}
void DestroyVtxStreamRange(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyVtxStream_180056cb0(ignoredThis, base, count);
}
const ArraySpec kSpec_VtxStream18 = {             // 0x180056af0
    "Array<Video::VertexProgram::Stream>", 0x18, 8, &AssignVtxStream18,
    &InitZero18, &DestroyVtxStreamRange, false,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0x00, 0x18 };   // header freed, String dereferenced

// --- 0x180053190  Array<Audio::SoundEffect::Buffer> -------------------------
// Element {float @+0x00, Collection::Array<Sample> @+0x08}, stride 0x18,
// align 8. Confirmed twice over: the element serialize walk 0x180053300 reads
// a float at +0x00 and the nested array's count at +0x10, and the destroy
// 0x180052220 is Body A with `offs = {0x08}` and stride 0x18 (`kD_InnerArr_18`).
//   assign  dword at +0x00, then zero the nested header and run its
//           `operator=` (0x180052700)              0x180053232..0x18005324e
//   init    dword 0 at +0x00, qword 0 at +0x08, qword 0 at +0x10
//           0x180053262..0x180053268 — +0x04..+0x07 stays uninitialised
static void SfxSampleAssign(Audio::SoundEffect::Sample* d, const Audio::SoundEffect::Sample* s) {
    d->startSeconds        = s->startSeconds;     // DWORD at +0x00
    d->variants._data      = nullptr;             // +0x08 = 0
    d->variants._size      = 0;                   // +0x10 = 0 (one qword, H7)
    d->variants._capacity  = 0;
    {
        RtColl_ArrayAssign_180052700(&d->variants, &s->variants);
    }
}
void AssignSfxBuffer18(uint8_t* d, const uint8_t* s) {
    AssignAs<Audio::SoundEffect::Sample, &SfxSampleAssign>(d, s);
}
static void SfxSampleInit(Audio::SoundEffect::Sample* d) {
    d->startSeconds       = 0.0f;                 // DWORD at +0x00
    d->variants._data     = nullptr;              // +0x08
    d->variants._size     = 0;                    // +0x10 (one qword, H7)
    d->variants._capacity = 0;
}
void InitSfxBuffer18(uint8_t* d) { InitAs<Audio::SoundEffect::Sample, &SfxSampleInit>(d); }
void DestroySfxBufferRange(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyInner_180052220(ignoredThis, base, count);
}
const ArraySpec kSpec_SfxBuffer18 = {             // 0x180053190
    "Array<Audio::SoundEffect::Buffer>", 0x18, 8, &AssignSfxBuffer18,
    &InitSfxBuffer18, &DestroySfxBufferRange, false,
    /*assign unwritten*/ 0x04, 0x08,
    /*init   unwritten*/ 0x04, 0x08,
    /*seed zero       */ 0x08, 0x18 };   // the nested header is freed

// --- 0x180052e60  Array<Audio::SoundEffect::Sample> -------------------------
// The nested array of the row above. Element {ExternalPtr @+0x00, float @+0x08,
// float @+0x0c, float @+0x10}, stride 0x18, align 8, FOUR-WAY UNROLLED in
// retail.
//   assign  three plain qword moves (0x180052f41..0x180052f54) — Assign18
//   init    two qword zeros and then the DWORD 0x3f800000 (1.0f) at +0x10
//           (0x180052f5f..0x180052f66); +0x14..+0x17 stays uninitialised
//   destroy NONE — the ExternalPtr is not released on a shrink, and the only
//           thing either arm does with the old buffer is free it
static void SfxVariantInit(Audio::SoundEffect::Variant* d) {
    d->buffer    = nullptr;                       // +0x00 qword
    d->pitchLow  = 0.0f;                          // +0x08 qword (pitchLow, pitchHigh)
    d->pitchHigh = 0.0f;
    d->volume    = 1.0f;                          // +0x10 DWORD 0x3f800000
}
void InitSfxSample18(uint8_t* d) { InitAs<Audio::SoundEffect::Variant, &SfxVariantInit>(d); }
const ArraySpec kSpec_SfxSample18 = {             // 0x180052e60
    "Array<Audio::SoundEffect::Sample>", 0x18, 8, &Assign18, &InitSfxSample18,
    nullptr, true,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0x14, 0x18,
    /*seed zero       */ 0,    0 };

#define D1_J1_ENTRY(ADDR)                                                     \
    void* RtColl_ArrayDtor_##ADDR(void* a) {                                  \
        ArrayDtorSited(static_cast<ArrayHdr*>(a));  /* CAST-OK: elem_stride (block-comment marker: a macro line cannot end in //) macro: the erased entry views its caller's Array<T> (void*) as the ArrayHdr core */ \
        return nullptr;                                                       \
    }

// Ten more COMDAT copies of the parameterless J1 body. Each was disassembled in
// full and is instruction-for-instruction the canonical body, resolving to the
// same four absolute operands (0x1801fc718, 0x18014b390, 0x18014b388,
// 0x1800c6c00) the J1 note at the top of this file pins.
D1_J1_ENTRY(180013e70)   // Graphics: EdgeMesh's Array<Triangle>, 0x180013640's clear
D1_J1_ENTRY(18001c020)   // Graphics: 0x18001b270's clear, Array<blk16>
D1_J1_ENTRY(18001c3b0)   // Graphics
D1_J1_ENTRY(180021490)   // Graphics: the particle emitter's Array<T>
D1_J1_ENTRY(18003d8c0)   // UI:       DocumentTextElement +0x110 / Description +0xb0
D1_J1_ENTRY(18003da00)   // UI:       DocumentTextElement +0x120 / SpriteCapture::_sheets
D1_J1_ENTRY(18003db40)   // UI:       DocumentTextElement +0x130 / Description +0xd0
D1_J1_ENTRY(18003e0c0)   // UI:       DocumentTextElement +0x148

#undef D1_J1_ENTRY

#define D1_J3_ENTRY2(ADDR, SPEC)                                              \
    void RtColl_ArraySetSize_##ADDR(void* a, int n) {                         \
        AsArray<&SPEC>(a)->SetSize(n);                                       \
    }

D1_J3_ENTRY2(18003e600, kSpec_PtrZero)        // UI: DocumentTextDescription +0xb0
D1_J3_ENTRY2(18003ea50, kSpec_PtrZero)        // UI: Description +0xc0 / SpriteCapture::_sheets
D1_J3_ENTRY2(18003eea0, kSpec_I32Z)           // UI: DocumentTextDescription +0xd0
D1_J3_ENTRY2(1800547e0, kSpec_StringRefZ_PP)  // Video: PixelProgram's string table
D1_J3_ENTRY2(180054a20, kSpec_PpSampler10)    // Video: PixelProgram::_samplers
D1_J3_ENTRY2(1800567d0, kSpec_VtxElement18)   // Video: VertexProgram::Stream::_elements
D1_J3_ENTRY2(180056af0, kSpec_VtxStream18)    // Video: VertexProgram::_streams
D1_J3_ENTRY2(180053190, kSpec_SfxBuffer18)    // Audio: SoundEffect::_buffers
D1_J3_ENTRY2(180052e60, kSpec_SfxSample18)    // Audio: SoundEffect::Buffer::_samples

#undef D1_J3_ENTRY2

#define D1_J3_ENTRY3(ADDR, SPEC)                                              \
    void RtColl_ArraySetSizeFill_##ADDR(void* a, int n, const void* fill) {   \
        AsArray<&SPEC>(a)->SetSizeFill(n, fill);                             \
    }

D1_J3_ENTRY3(18003df80, kSpec_DocItem24)      // UI: DocumentTextElement::_items append
D1_J3_ENTRY3(18003e1a0, kSpec_Link18)         // UI: DocumentTextElement::_links append

#undef D1_J3_ENTRY3

// --- The two constant-folded one-argument forms -----------------------------
// 0x18003d1c0 and 0x18003d430 are `SetSize(0)` with the new size folded to the
// immediate 0 — the shape 0x18002aae0 and 0x1800783d0 already have. They are
// driven through SetSizeCore rather than hand-written as `_size = 0` so that
// the branches retail cannot reach here (the grow test, and the copy loop,
// which MSVC compiled as `JS` because `i < 0` is never true) are reproduced
// rather than assumed away.
//
// 0x18003d430's shrink arm IS reachable and is the whole point of the call:
// it runs 0x18003e3e0 over the old elements, which is how the link table
// releases its Strings before it is rebuilt.
void RtColl_ArraySetSize_18003d1c0(void* a) {
    AsArray<&kSpec_DocItem24>(a)->SetSize(0);
}
void RtColl_ArraySetSize_18003d430(void* a) {
    AsArray<&kSpec_Link18>(a)->SetSize(0);
}


// ============================================================================
// CUTOVER CLOSURE BATCH D2 "helpers-high" (2026-09-12)
// ============================================================================
//
// The `Collection::Array<T>` half of the 48 unnamed helper bodies at or above
// 0x1800571b0 that today's ports reach BY ADDRESS. Four families, all of them
// this file's existing jobs:
//
//   J1  seven more copies of the parameterless `~Array` body
//   J3  thirteen more `SetSize` instantiations (two-, three- and one-argument)
//   J2  the ~Array-with-element-destroy pairs -> array_destroy.cpp
//   J4  six more `operator=`                  -> array_destroy.cpp
//
// The owners, recovered from `get_function_callers` in retail Ghidra:
//   0x180061a80..0x180062320  Graphics::Particle        (Serialize 0x180103f00)
//   0x180065bf0..0x180066fa0  Collision2D::CollisionMesh(Serialize 0x180108cd0)
//   0x1800734a0..0x180074360  Picking::PickingMesh      (SubMesh members)
//   0x180076a40..0x1800774d0  UI::Font                  (Serialize 0x1801176d0,
//                                                        ComputeStringLayout)
//   0x18007a350..0x18007ac20  Picking::PickingMesh      (Serialize 0x180122960)
//   0x1800571b0               Video::VertexProgram      (Serialize 0x1800f6240)
//
// *** TWO EXISTING FILES CARRIED PRIVATE COPIES OF FOUR OF THESE BODIES. ***
// src/engine/collision2d/collisionmesh.cpp and src/engine/picking/
// pickingmesh.cpp each had a `GenericArraySetSize` template plus hand-written
// Face / SubMesh resizes, written before this family was parameterised. They
// were NOT byte-faithful -- `GenericArraySetSize` zero-fills the new tail for
// EVERY element type, and three of the instantiations it stood in for
// (0x18007a790, 0x18007a900, 0x1800774d0) have no `else` arm at all, while
// pickingmesh.cpp's `SubMeshArraySetSize` skipped retail's per-element destroy
// on BOTH the shrink and the realloc arm and did a shallow struct copy where
// retail deep-copies (String refcount + four inner `operator=`). Those copies
// are DELETED in this same commit and both files now call these bodies by
// address; there is one body per address, which is the rule this batch was
// told to keep.
//
// ---------------------------------------------------------------------------
// J1 -- the seven new `~Array` addresses
// ---------------------------------------------------------------------------
// MEASURED, not eyeballed (reference_position_only_diffs_hide_one_body.md): all
// seven were read with `read_memory` over their full 115 bytes and compared
// against the canonical 0x180033960. Outside the FIVE relocatable operands --
// the disp32 of `MOV RBX,[rip]` (+36), of `CALL [rip]` (+58), of
// `MOV RCX,[rip]` (+68), the `CALL rel32` (+75) and the second `CALL [rip]`
// (+84) -- every one of the 115 bytes is equal, and all five resolve in ALL
// SEVEN to the same absolutes the J1 banner pins:
//     0x1801fc718, 0x18014b390, 0x1801fc718, 0x1800c6c00, 0x18014b388
// So the "no parameters at all, not even a stride" conclusion covers them
// unchanged and one body still serves them all.
#define J1_ENTRY(ADDR)                                                        \
    void* RtColl_ArrayDtor_##ADDR(void* a) {                                  \
        ArrayDtorSited(static_cast<ArrayHdr*>(a));  /* CAST-OK: elem_stride (block-comment marker: a macro line cannot end in //) macro: the erased entry views its caller's Array<T> (void*) as the ArrayHdr core */ \
        return nullptr;                                                       \
    }

J1_ENTRY(180065bf0)   // Collision2D: CollisionMesh::_ptsA (+0x30) AND _ptsB (+0x40)
J1_ENTRY(180065e80)   // Collision2D: CollisionMesh::_surfaces (+0x60)

#undef J1_ENTRY

// ---------------------------------------------------------------------------
// The new element operations
// ---------------------------------------------------------------------------

// 0x1800774d0 (UI::Font's uint16 array). `MOV AX,[..]; MOV [..],AX`.
void AssignD2_2(uint8_t* d, const uint8_t* s) { AssignAs<uint16_t, &CopyWords<uint16_t, 1>>(d, s); }

// 0x180066e80 / 0x180066de0 (Collision2D::Face::triples, stride SIX -- the
// element is three uint16). The copy is a DWORD at +0x00 and a WORD at +0x04,
// which is how MSVC moves six bytes; it is written with those two widths and
// not as a memcpy because a hardware write watchpoint sees the difference.
using CollisionTriple = Collision2D::CollisionMesh::Triple3;
static void TripleAssign(CollisionTriple* d, const CollisionTriple* s) {
    d->a = s->a;                         // one DWORD at +0x00 (a, b) in retail
    d->b = s->b;
    d->c = s->c;                         // one WORD at +0x04
}
void AssignD2_6(uint8_t* d, const uint8_t* s) { AssignAs<CollisionTriple, &TripleAssign>(d, s); }
// 0x180066e80's `else` arm, in RETAIL'S OWN ORDER: the DWORD at +0x02 is
// stored FIRST and the WORD at +0x00 second (`MOV [RCX+2],0` then
// `MOV [RCX],0`). The two overlap at +0x02..+0x03; the final state is the same
// either way, but the order is what the allocator and a watchpoint see.
static void TripleInit(CollisionTriple* d) {
    d->b = 0;                            // the DWORD at +0x02 (b, c) FIRST
    d->c = 0;
    d->a = 0;                            // then the WORD at +0x00
}
void InitZeroD2_6(uint8_t* d) { InitAs<CollisionTriple, &TripleInit>(d); }

// 0x18007a790 / 0x180073dc0 (Picking::SubMesh::_triangles, stride 0xc): a QWORD
// at +0x00 and a DWORD at +0x08. NOT `Assign12` (three dwords) -- same twelve
// bytes, different store widths, and the two rows are kept apart for that
// reason alone.
using PickingTriangle = Picking::PickingMesh::Triangle;
static void TriangleAssign(PickingTriangle* d, const PickingTriangle* s) {
    d->i0 = s->i0;                       // one QWORD at +0x00 (i0, i1, i2, j0)
    d->i1 = s->i1;
    d->i2 = s->i2;
    d->j0 = s->j0;
    d->j1 = s->j1;                       // one DWORD at +0x08 (j1, j2)
    d->j2 = s->j2;
}
void AssignD2_12q(uint8_t* d, const uint8_t* s) { AssignAs<PickingTriangle, &TriangleAssign>(d, s); }
// 0x18007a4d0's `else` arm: one zero QWORD at +0x00 and one zero DWORD at
// +0x08, so the whole 0xc stride is written.
static void TriangleInit(PickingTriangle* d) {
    d->i0 = 0;                           // one zero QWORD at +0x00
    d->i1 = 0;
    d->i2 = 0;
    d->j0 = 0;
    d->j1 = 0;                           // one zero DWORD at +0x08
    d->j2 = 0;
}
void InitZeroD2_12q(uint8_t* d) { InitAs<PickingTriangle, &TriangleInit>(d); }

// 0x180061bb0 (the live-particle slot array INSIDE a Graphics::Particle pool
// record -- the record ctor 0x180103bf0 sizes it from the Particle's +0x28;
// src/engine/graphics/Particle.h has the full record layout). The copy arm
// is TWO qwords at +0x00/+0x08, TWELVE dwords at +0x10..+0x3f, then THREE
// qwords at +0x40/+0x48/+0x50 -- 0x58 bytes with no hole, so this is a pure
// byte copy and `byteCopy` is true. The widths are transcribed rather than
// folded into a memcpy for the reason AssignD2_6 gives.
static void ParticleRecordAssign(RtGraphics::ParticleRecord* d, const RtGraphics::ParticleRecord* s) {
    *d = *s;      // two qwords, twelve dwords, three qwords in retail: all 0x58 bytes
}
void AssignD2_58(uint8_t* d, const uint8_t* s) {
    AssignAs<RtGraphics::ParticleRecord, &ParticleRecordAssign>(d, s);
}
// 0x180061bb0's `else` arm: ELEVEN zero qwords, +0x00..+0x57, no hole.
void InitZeroD2_58(uint8_t* d) { InitAs<uint64_t, &ZeroWords<uint64_t, 11>>(d); }

// ---- the two NON-TRIVIAL elements -----------------------------------------
//
// Both element types own inner `Collection::Array<T>`, so their assign is a
// CALL to a real copy constructor and their destroy a CALL to a range helper.
// The callees are ported in the same commit (the copy ctors below, the range
// helpers in array_destroy.cpp) and are reached BY ADDRESS -- registry-first,
// so the day either is re-registered elsewhere this file follows without an
// edit, exactly like kSpec_PkgEntry's DestroyPkgEntryRange.

// Declared, not duplicated: these six `Array<T>::operator=` bodies live in
// array_destroy.cpp (they are ArrayAssignCore instantiations). A second copy of
// any of them here is exactly the drift the shared core exists to prevent.
void* RtColl_ArrayAssign_180066de0(void* self, const void* src);   // Face::triples
void* RtColl_ArrayAssign_180066b30(void* self, const void* src);   // Face::quads
void* RtColl_ArrayAssign_1800741b0(void* self, const void* src);   // SubMesh::_vertices/_normals
void* RtColl_ArrayAssign_180073dc0(void* self, const void* src);   // SubMesh::_triangles
void* RtColl_ArrayAssign_1800742d0(void* self, const void* src);   // SubMesh::_uvs
// The two range destroys, likewise in array_destroy.cpp.
void RtColl_DestroyInner_1800664c0(void* ignoredThis, void* base, int count);
void RtColl_DestroySubMesh_180073ee0(void* ignoredThis, void* base, int count);

// 0x180065b70 -- `Collision2D::CollisionMesh::Face::Face(const Face&)`, the
// element copy constructor 0x180066350's grow arm CALLs. Read out of its own
// body, in retail's order:
//     MOV EAX,[RDX]      ; MOV [RCX],EAX            field0, a DWORD at +0x00
//     the two qwords of the inner Array header at +0x08 zeroed
//     CALL 0x180066de0   ; Array<Triple3>::operator= at +0x08
//     the two qwords of the inner Array header at +0x18 zeroed
//     CALL 0x180066b30   ; Array<Quad4>::operator=   at +0x18
//     MOV RAX,RCX
// +0x04..+0x07 (the padding after field0) is NEVER WRITTEN by this arm, which
// is the assign window below. The pre-zeroing is load-bearing and not tidying:
// each `operator=` opens by destroying the DESTINATION, so a non-zero header
// there would be freed as a live buffer.
using CollisionFace = Collision2D::CollisionMesh::Face;
static void FaceAssign(CollisionFace* d, const CollisionFace* s) {
    d->field0 = s->field0;
    d->triples._data     = nullptr;
    d->triples._size     = 0;            // one qword in retail (H7)
    d->triples._capacity = 0;
    RtColl_ArrayAssign_180066de0(&d->triples, &s->triples);
    d->quads._data     = nullptr;
    d->quads._size     = 0;
    d->quads._capacity = 0;
    RtColl_ArrayAssign_180066b30(&d->quads, &s->quads);
}
void* AssignFace28Impl(uint8_t* d, const uint8_t* s) {
    AssignAs<CollisionFace, &FaceAssign>(d, s);
    return d;
}
void AssignFace28(uint8_t* d, const uint8_t* s) { AssignFace28Impl(d, s); }
// 0x180066350's `else` (init) arm: FOUR zero qwords at +0x08, +0x10, +0x18 and
// +0x20 -- and NOTHING at +0x00..+0x07. A fresh Face's `field0` and its padding
// are left holding whatever the allocation contained, which is why the init
// window below is +0x00..+0x08 and not empty.
static void FaceInit(CollisionFace* d) {
    d->triples._data     = nullptr;      // +0x08
    d->triples._size     = 0;            // +0x10 (one qword, H7)
    d->triples._capacity = 0;
    d->quads._data       = nullptr;      // +0x18
    d->quads._size       = 0;            // +0x20 (one qword, H7)
    d->quads._capacity   = 0;
}
void InitFace28(uint8_t* d) { InitAs<CollisionFace, &FaceInit>(d); }
void DestroyFace28Range(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroyInner_1800664c0(ignoredThis, base, count);
}

// 0x1800734a0 -- `Picking::SubMesh::SubMesh(const SubMesh&)`, the element copy
// constructor 0x18007aa40's grow arm CALLs. Read out of its own body:
//     +0x00  System::String  zero-then-adopt-then-INC [str-4]
//     +0x08..+0x3f  fourteen plain DWORD moves (_parent, the transform, the AABB)
//     +0x40  inner Array header zeroed, then CALL 0x1800741b0  (_vertices)
//     +0x50  inner Array header zeroed, then CALL 0x1800741b0  (_normals)
//     +0x60  inner Array header zeroed, then CALL 0x180073dc0  (_triangles)
//     +0x70  inner Array header zeroed, then CALL 0x1800742d0  (_uvs)
// Every one of the 0x80 bytes is written, so there is no assign window.
using PickingSubMesh = Picking::PickingMesh::SubMesh;
static void SubMeshAssign(PickingSubMesh* d, const PickingSubMesh* s) {
    // the String at +0x00: null the slot, adopt, bump the shared refcount
    AdoptString(d->_name._data, s->_name._data);
    // +0x08..+0x3f: fourteen plain DWORD moves in retail
    d->_parent = s->_parent;
    d->_local  = s->_local;
    d->_aabb   = s->_aabb;
    d->_vertices._data = nullptr;  d->_vertices._size = 0;  d->_vertices._capacity = 0;
    RtColl_ArrayAssign_1800741b0(&d->_vertices, &s->_vertices);
    d->_normals._data = nullptr;   d->_normals._size = 0;   d->_normals._capacity = 0;
    RtColl_ArrayAssign_1800741b0(&d->_normals, &s->_normals);
    d->_triangles._data = nullptr; d->_triangles._size = 0; d->_triangles._capacity = 0;
    RtColl_ArrayAssign_180073dc0(&d->_triangles, &s->_triangles);
    d->_uvs._data = nullptr;       d->_uvs._size = 0;       d->_uvs._capacity = 0;
    RtColl_ArrayAssign_1800742d0(&d->_uvs, &s->_uvs);
}
void* AssignSubMesh80Impl(uint8_t* d, const uint8_t* s) {
    AssignAs<PickingSubMesh, &SubMeshAssign>(d, s);
    return d;
}
void AssignSubMesh80(uint8_t* d, const uint8_t* s) { AssignSubMesh80Impl(d, s); }
// 0x18007aa40's `else` arm, the literal default-constructed SubMesh: name null,
// parent -1, identity rotation (1.0f at +0x18), an INVERTED AABB (+1e8 min,
// -1e8 max -- 0x4cbebc20 / 0xccbebc20) and four empty inner Arrays. All 0x80
// bytes are written; there is no init window.
static void SubMeshInit(PickingSubMesh* d) {
    d->_name._data             = nullptr;   // +0x00
    d->_parent                 = -1;        // +0x08 0xffffffff
    d->_local.rotation.x       = 0.0f;      // +0x0c qword
    d->_local.rotation.y       = 0.0f;
    d->_local.rotation.z       = 0.0f;      // +0x14
    d->_local.rotation.w       = 1.0f;      // +0x18 qword 0x3f800000 (w, translation.x)
    d->_local.translation.x    = 0.0f;
    d->_local.translation.y    = 0.0f;      // +0x20 qword
    d->_local.translation.z    = 0.0f;
    d->_aabb.min.x             = 1.0e8f;    // 0x4cbebc20
    d->_aabb.min.y             = 1.0e8f;
    d->_aabb.min.z             = 1.0e8f;
    d->_aabb.max.x             = -1.0e8f;   // 0xccbebc20
    d->_aabb.max.y             = -1.0e8f;
    d->_aabb.max.z             = -1.0e8f;
    d->_vertices._data  = nullptr; d->_vertices._size  = 0; d->_vertices._capacity  = 0;
    d->_normals._data   = nullptr; d->_normals._size   = 0; d->_normals._capacity   = 0;
    d->_triangles._data = nullptr; d->_triangles._size = 0; d->_triangles._capacity = 0;
    d->_uvs._data       = nullptr; d->_uvs._size       = 0; d->_uvs._capacity       = 0;
}
void InitSubMesh80(uint8_t* d) { InitAs<PickingSubMesh, &SubMeshInit>(d); }
void DestroySubMesh80Range(void* ignoredThis, uint8_t* base, int count) {
    RtColl_DestroySubMesh_180073ee0(ignoredThis, base, count);
}

// ---------------------------------------------------------------------------
// The specs. Every field is read out of ITS OWN body -- never from a sibling
// with the same stride (this batch has four distinct stride-8 rows whose only
// difference is the `MOV EDX,<align>` immediate, and three distinct stride-0xc
// rows).
// ---------------------------------------------------------------------------

// 0x180062110  Graphics::Particle::Serialize's record-pointer array.
//   SHL RCX,0x3 / MOV EDX,0x8 -> stride 8, align 8
//   a plain qword move, no zero-first store -> Assign8
//   NO else arm: the new tail is left UNINITIALISED.
// That is kSpec_Fill8 exactly, so it is REUSED rather than duplicated.

// 0x1800660f0  Collision2D::CollisionMesh::_ptsA (Vec2, two floats).
//   SHL RCX,0x3 / MOV EDX,0x4 -> stride 8, ALIGN 4
//   else arm writes one zero qword -> InitZero8.
// NOT kSpec_U64Zero (align 8) and NOT kSpec_Fill8 (no init arm).
const ArraySpec kSpecD2_Blk8A4Z = {               // 0x1800660f0
    "Array<Vec2>(align 4, zero tail)", 8, 4, &Assign8, &InitZero8, nullptr,
    true, 0, 0, 0, 0, 0, 0 };

// 0x1800666e0  CollisionMesh::_ptsB AND ::_surfaces (retail shares one body).
//   SHL RCX,0x3 / MOV EDX,0x8 -> stride 8, align 8; else arm one zero qword.
// Parameter-identical to kSpec_U64Zero; given its own row because the name is
// what the diff summary prints and "Array<Time>" on this array would be the
// kind of label that gets believed (the reason kSpec_Ptr8Zero exists too).
const ArraySpec kSpecD2_Blk8A8Z = {               // 0x1800666e0
    "Array<Vec2/ExternalPtr>(zero tail)", 8, 8, &Assign8, &InitZero8, nullptr,
    true, 0, 0, 0, 0, 0, 0 };

// 0x180066bc0  CollisionMesh::Face::quads (four uint16 = 8 bytes).
//   SHL RCX,0x3 / MOV EDX,0x2 -> stride 8, ALIGN 2; else arm one zero qword.
const ArraySpec kSpecD2_Blk8A2Z = {               // 0x180066bc0
    "Array<Quad4>(align 2, zero tail)", 8, 2, &Assign8, &InitZero8, nullptr,
    true, 0, 0, 0, 0, 0, 0 };

// 0x180066e80  CollisionMesh::Face::triples (three uint16 = SIX bytes).
//   `IMUL RCX,RCX,0x6` / MOV EDX,0x2 -> stride 6, align 2.
// The first row in this file with an ODD-sized element that is not 1.
const ArraySpec kSpecD2_Edge6A2Z = {              // 0x180066e80
    "Array<Triple3>", 6, 2, &AssignD2_6, &InitZeroD2_6, nullptr,
    true, 0, 0, 0, 0, 0, 0 };

// 0x180066350  CollisionMesh::_faces. NON-TRIVIAL: element copy ctor
// 0x180065b70, shrink/realloc destroy 0x1800664c0.
const ArraySpec kSpecD2_Face28 = {                // 0x180066350
    "Array<Collision2D::Face>", 0x28, 8,
    &AssignFace28, &InitFace28, &DestroyFace28Range, false,
    /*assign unwritten*/ 0x04, 0x08,   // the pad after field0
    /*init   unwritten*/ 0x00, 0x08,   // field0 AND its pad
    /*seed zero       */ 0x08, 0x28 }; // both inner Array headers are destroyed

// 0x1800774d0  UI::Font::Serialize's uint16 scratch array.
//   `ADD RCX,RCX` (stride 2) / MOV EDX,0x2 -> stride 2, align 2; no else arm.
const ArraySpec kSpecD2_U16A2 = {                 // 0x1800774d0
    "Array<uint16>", 2, 2, &AssignD2_2, nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };

// 0x180077330 / 0x180076d10 / 0x180076a40  UI::Font's 16-byte glyph records
// (Serialize's glyph table, ComputeLineLayout's append, ComputeStringLayout's
// clear). One element type, three arities:
//   SHL RCX,0x4 / MOV EDX,0x4 -> stride 0x10, ALIGN 4
//   four dword moves per element, no else arm.
// NOT kSpec_Fill16 (align 8) and NOT kSpec_Fill16A4 (which is the same two
// numbers but was read out of a different body -- kept separate because the
// name is what a divergence report prints).
const ArraySpec kSpecD2_Glyph10A4 = {             // 0x180077330, 0x180076d10,
    "Array<UI::Font::Glyph16>", 0x10, 4,          // 0x180076a40
    &Assign16, nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };

// 0x18007a4d0  Picking::SubMesh::_vertices AND ::_normals (Vector3).
//   `LEA RCX,[RAX+RAX*2]; SHL RCX,0x2` (stride 0xc) / MOV EDX,0x4
//   assign = THREE dword moves; else arm = one zero qword + one zero dword.
const ArraySpec kSpecD2_Vec12A4Z = {              // 0x18007a4d0
    "Array<Vector3>", 0xc, 4, &Assign12, &InitZeroD2_12q, nullptr,
    true, 0, 0, 0, 0, 0, 0 };

// 0x18007a790  Picking::SubMesh::_triangles (two uint16 triples = 0xc bytes).
//   stride 0xc / MOV EDX,0x2 -> ALIGN 2
//   assign = one QWORD + one DWORD (not three dwords -- see AssignD2_12q)
//   NO else arm: the new tail is left UNINITIALISED.
const ArraySpec kSpecD2_Tri12A2 = {               // 0x18007a790
    "Array<Picking::Triangle>", 0xc, 2, &AssignD2_12q, nullptr, nullptr,
    true, 0, 0, 0, 0, 0, 0 };

// 0x18007a900  Picking::SubMesh::_uvs (two uint16).
//   SHL RCX,0x2 / MOV EDX,0x2 -> stride 4, ALIGN 2; no else arm.
// NOT kSpec_Fill4, whose align is 4.
const ArraySpec kSpecD2_U32A2 = {                 // 0x18007a900
    "Array<Picking::UV16>", 4, 2, &Assign4, nullptr, nullptr, true, 0, 0, 0, 0, 0, 0 };

// 0x18007aa40  Picking::PickingMesh::_nodes. NON-TRIVIAL: element copy ctor
// 0x1800734a0, shrink/realloc destroy 0x180073ee0 (four inner Arrays AND a
// String per element).
const ArraySpec kSpecD2_SubMesh80 = {             // 0x18007aa40
    "Array<Picking::SubMesh>", 0x80, 8,
    &AssignSubMesh80, &InitSubMesh80, &DestroySubMesh80Range, false,
    /*assign unwritten*/ 0,    0,
    /*init   unwritten*/ 0,    0,
    /*seed zero       */ 0x40, 0x80 }; // the four inner Array headers

// 0x180061bb0  the pool record's live-particle slot array (record+0x18),
// sized by the record ctor 0x180103bf0 from Particle+0x28.
//   `IMUL RCX,RCX,0x58` / MOV EDX,0x8 -> stride 0x58, align 8.
const ArraySpec kSpecD2_Emit58 = {                // 0x180061bb0
    "Array<Graphics::ParticleSlot58>", 0x58, 8,
    &AssignD2_58, &InitZeroD2_58, nullptr, true, 0, 0, 0, 0, 0, 0 };

// ---- the entry points ------------------------------------------------------
#define J3_ENTRY2(ADDR, SPEC)                                                 \
    void RtColl_ArraySetSize_##ADDR(void* a, int n) {                         \
        AsArray<&SPEC>(a)->SetSize(n);                                       \
    }

J3_ENTRY2(180062110, kSpec_Fill8)          // Graphics:    Particle record ptrs
J3_ENTRY2(180061bb0, kSpecD2_Emit58)       // Graphics:    Particle pool-record slots
J3_ENTRY2(1800660f0, kSpecD2_Blk8A4Z)      // Collision2D: CollisionMesh::_ptsA
J3_ENTRY2(1800666e0, kSpecD2_Blk8A8Z)      // Collision2D: _ptsB and _surfaces
J3_ENTRY2(180066bc0, kSpecD2_Blk8A2Z)      // Collision2D: Face::quads
J3_ENTRY2(180066e80, kSpecD2_Edge6A2Z)     // Collision2D: Face::triples
J3_ENTRY2(180066350, kSpecD2_Face28)       // Collision2D: _faces   [NON-TRIVIAL]
J3_ENTRY2(1800774d0, kSpecD2_U16A2)        // UI:          Font's uint16 scratch
J3_ENTRY2(180077330, kSpecD2_Glyph10A4)    // UI:          Font's glyph table
J3_ENTRY2(18007a4d0, kSpecD2_Vec12A4Z)     // Picking:     SubMesh::_vertices/_normals
J3_ENTRY2(18007a790, kSpecD2_Tri12A2)      // Picking:     SubMesh::_triangles
J3_ENTRY2(18007a900, kSpecD2_U32A2)        // Picking:     SubMesh::_uvs
J3_ENTRY2(18007aa40, kSpecD2_SubMesh80)    // Picking:     _nodes   [NON-TRIVIAL]

#undef J3_ENTRY2

// 0x180076d10 -- the THREE-argument form of the glyph array: UI::Font::
// ComputeLineLayout appends one record by passing `size + 1` and a pointer to
// it. Same spec as 0x180077330; different arity, so a separate entry point,
// because registering it behind the two-argument signature would read a
// register the caller never set.
void RtColl_ArraySetSizeFill_180076d10(void* a, int n, const void* fill) {
    AsArray<&kSpecD2_Glyph10A4>(a)->SetSizeFill(n, fill);
}

// 0x180076a40 -- the SAME glyph array with the new size folded to the constant
// 0, the one-argument shape 0x1800783d0 and 0x18002aae0 already have.
//
// IT IS NOT A MEMBER OF THE J1 `~Array` FAMILY, and reading it as one would be
// a live defect rather than a mislabel. `~Array` frees unconditionally; this
// body's grow test is `CMP dword [RCX+0xc],0 / JGE skip` and capacity is never
// negative, so the MemAlloc and the whole copy loop are unreachable with a new
// size of 0, the epilogue's `CMP <old>,[RCX]` is always equal, and the MemFree
// never runs. The observable effect is `_size = 0` with the buffer and the
// capacity KEPT -- which is exactly what UI::Font::ComputeStringLayout wants of
// its per-line scratch: it clears it once per line and would thrash the heap if
// this freed. SetSizeCore is still the implementation rather than a written-out
// `_size = 0`, so that if this reading is ever wrong the two disagree under the
// oracle instead of agreeing by construction.
void RtColl_ArraySetSize_180076a40(void* a) {
    AsArray<&kSpecD2_Glyph10A4>(a)->SetSize(0);
}

// ---- the two element COPY CONSTRUCTORS, at their own addresses -------------
// AssignFace28Impl / AssignSubMesh80Impl above ARE 0x180065b70 and 0x1800734a0,
// so they get entry points with retail's signature (RCX = dst, RDX = src,
// return RAX = dst) rather than a second body. Registering them matters at
// cutover: retail's SetSize reaches each by rel32, and our SetSize reaches
// them through the ArraySpec, but a THIRD caller resolving the address (or the
// mint) would otherwise find nothing.


}  // namespace rtcoll

// ============================================================================
// Registration
// ============================================================================
//
// `RegisterRuntimeAddr` and not `RegisterRuntimeExport` for all 27: these
// instantiations have no exported name in either build, so there is nothing to
// register under. tools/runtime_census.py counts address registrations from the
// literal in the call, so every address below is spelled as a literal here even
// though the entry-point name already contains it
// (reference_census_cannot_see_nonliteral_registrations.md: twelve live ports
// were invisible to the census for a full wave because the name was a named
// constant).
//
// *** THIS REGISTRATION CHANGES BEHAVIOUR AT ~20 EXISTING CALL SITES. ***
// `ResolveAddr(Module::Runtime, …)` consults the port registry FIRST
// (reference_resolveaddr_consults_port_registry_first.md), so from the moment
// this runs, src/engine/{graphics,picking,ui,audio,achievement,engine}/… and
// engine/funcs/engine_funcs.cpp stop reaching the DLL's Array bodies and reach
// these. Several of those files carried comments asserting the opposite; they
// are corrected in the same commit. If this lane is reverted, revert those too.
// ---------------------------------------------------------------------------
// ENDGAME B.3.2 CLOSING PASS (2026-09-18) — 67 ENTRY POINTS AND ROWS REMOVED.
//
// Each had NO caller in the tree by NAME and NO reference to its retail address
// in CODE: not a `ResolveAddr(Module::Runtime, 0x18…)`, not a second
// `RegisterRuntimeAddr`, not one of the wrapper forms (`RtAddr<Fn>(0x…)`,
// `RtAddrFn`, `ArrayDtor(0x…, p)`, `ArraySetSize(0x…, …)`, `SetSizeWithFill`,
// `PlainArrayDtor`, `ByAddr(0x…)`), not a bare-hex table entry, and not
// `standalone/rt_export_addrs.inc` or `rt_data_mint_tables.inc` (ZERO
// collection addresses appear in either). The only remaining mentions are the
// provenance comments in this file and its neighbours, which are still true
// statements about the DLL and are left standing — so if you find a banner
// below naming an address with no entry, THIS is why. The closure is at zero
// unported engine functions, so there is no future Runtime caller either.
//
// Removed (retail addresses):
//   J1  ~Array:        180004890 18001bf50 18001c9e0 180033960 180033a40
//                      18005fc20 1800737f0 1800739e0 180073bb0 180037240
//                      18002fb70 18002a930 180051ef0 180005800 1800097f0
//                      180008e80 180009700 180033bc0 180033de0 1800527a0
//                      180066cd0 180066fa0 180073e60 180074250 180074360
//   J3  SetSize:       180008b90 180008d60 180008ff0 18007b960 18002a9d0
//                      180052330 180005110 18002c6a0 180078c90 180078950
//                      180075d20 180075b20 180079df0 180079c90 180051390
//                      180009d70 180034010 180051020 18002aae0 18002a810
//   J3  SetSize(fill): 18001f990 18005fb00 180048fe0 1800417d0 180078520
//                      180076250 180076370 18004bde0 180051aa0 180004e30
//                      180061480 18002ffa0 180053da0 180063680 180033080
//                      1800332d0 1800638e0 18002ae20
//   other:             18002fd90 (Reserve)  180050e00 (Remove)
//                      180065b70 1800734a0 (element copy ctors)
// ---------------------------------------------------------------------------
