// ============================================================================
// src/engine/graphics/wave21_lane_gfxbody.cpp — Wave 21 lane GFXBODY
// ============================================================================
//
// Three of the thirteen CODE rows of the Graphics census gap. The triage that
// picked these three out of the thirteen, with a measured size and callee list
// for every row and the re-check of four inherited holdbacks, is
// docs/history/runtime_port/WAVE21_LANE_GFXBODY.md. Read §3 before adding a fourth.
//
//   | Export                                     | Retail      | Ord  | Bytes |
//   |--------------------------------------------|-------------|------|-------|
//   | ??2FrameCopyImageBuffer@Graphics@@SAPEAX_K@Z| 0x18001efc0 |  787 |    99 |
//   | ??2MaterialInstance@Graphics@@SAPEAX_K@Z    | 0x180002820 |  823 |    99 |
//   | ?Setup@MaterialInstance@Graphics@@QEBAX...  | 0x180102c90 | 4028 |   332 |
//
// All three names, and every callee named below, were checked against the
// export directory of the shipped C:\GOG Games\Banished\Runtime-x64.dll with
// pefile BEFORE a line was written; the addresses and ordinals above are that
// query's output (reference_resolveexport_phantom_names.md).
//
// GHIDRA WAS NOT AVAILABLE to this lane — the MCP bridge refused connection for
// its whole run. Every claim below is from `objdump -d` on the shipped DLL over
// the function's FULL extent, where "full extent" means the union of its
// `.pdata` fragments grouped by unwind chain, NOT the single `.pdata` row that
// contains the entry address. That distinction cost this lane a wrong number
// once already (WAVE21_LANE_GFXBODY.md §1.1) and is the reason no size in this
// file came from a single record.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "system/MemoryAllocator.h"
#include "graphics/Material.h"
#include "video/VideoProgram.h"   // ENDGAME B.3.5 caller batch
#include "video/Context.h"

using RtGraphics::MaterialBinding;
using RtGraphics::MaterialConstant;
using RtGraphics::MaterialParameterSet;
using RtGraphics::MaterialTexture;
using RtVideo::Context;

namespace {

// ============================================================================
// THE OFFSETS THIS FILE ACTUALLY TOUCHES.
//
// Both layouts are already pinned by earlier waves, so this lane adds no new
// struct — it asserts against the two existing headers instead, which is the
// stronger check: if another lane moves a field, this file stops compiling.
// Every offset below was independently re-read out of Setup's disassembly by
// this lane, and all nine agreed with the header. That agreement is worth
// stating: Material.h's own comments say MaterialBinding, MaterialParameterSet
// and MaterialConstant were pinned BY THIS FUNCTION in Wave 6 — so Wave 6
// reversed Setup, wrote its layout down, and then held the body back on the
// ground that it "read layouts this lane did not pin".
// ============================================================================
static_assert(offsetof(Graphics::MaterialInstance, _material)   == 0x28, "Setup reads this+0x28");
static_assert(offsetof(Graphics::MaterialInstance, _parameters) == 0x30, "Setup reads this+0x30");
static_assert(offsetof(Graphics::MaterialInstance, _constants)  == 0x40, "Setup reads this+0x40");
static_assert(offsetof(Graphics::MaterialInstance, _textures)   == 0x50, "Setup reads this+0x50");
static_assert(sizeof(MaterialParameterSet) == 0x20, "shl $0x5 on the shader index");
static_assert(sizeof(MaterialBinding)      == 0x08, "add $0x8 per binding");
static_assert(sizeof(MaterialTexture)      == 0x10, "2*index then *8 == index*0x10");
static_assert(sizeof(MaterialConstant)     == 0x18, "lea (rax,rax,2) then *8 == index*0x18");
static_assert(offsetof(MaterialConstant, value) == 0x08, "float4 at +0x08 of the constant");

static_assert(offsetof(Context, _dirty)            == 0x0010, "Setup ORs ctx+0x10");
static_assert(offsetof(Context, _imageBufferDirty) == 0x0018, "Setup ORs ctx+0x18");
static_assert(offsetof(Context, _imageBuffers)     == 0x0030, "Setup writes ctx+0x30+slot*8");
static_assert(offsetof(Context, _pixelConstantStaging) == 0x0200,
              "Setup writes ctx+0x208+slot*0x10, i.e. staging[0x08 + slot*0x10]");

// The two dirty bits Setup raises, by VALUE not by the enum name.
//
// 0x2 is Context.h's `kDirtyImageBuffers` and that name is safe to use. 0x8000
// is NOT in Context.h's enum at all — the enum's `kDirtyMaterialConstants` is
// 0x1000, and Context.h:130-149 documents at length that that name is
// MISLEADING (0x1000 feeds the INSTANCE constant buffer; 0x8000 is the one
// BindResources uploads from +0x208, length 0x40). Using the literal here, with
// this note, is deliberate: borrowing a name whose own header says it means
// something else is how a wrong constant survives review.
constexpr uint32_t kDirtyImageBuffersBit    = 0x00000002u;
constexpr uint32_t kDirtyMaterialStagingBit = 0x00008000u;

// The +0x208 staging window is 0x40 bytes = four float4 slots. That is the
// length BindResources uploads (Context.h:139), and it is what bounds the slot
// index Setup writes. Not enforced at runtime — retail does not bound it either
// and this port stays faithful — but recorded so the next reader knows the
// shape.
constexpr size_t kMaterialStagingOffset = 0x208 - 0x200;   // into _pixelConstantStaging



// ---------------------------------------------------------------------------
// ATTENDANCE, not a diff oracle. Three bodies, three counters.
//
// This exists because a registered port with no caller reports a clean run and
// reads as verification (reference_uncalled_port_is_not_verified.md), and
// because the A/B control for these three is `RT_GFXBODY=0`, under
// which the registrations do not happen at all -- so the flag-off arm MUST show
// zero here. If it did not, the "kill switch" would not be killing anything,
// which is the failure reference_the_gate_was_never_a_control.md describes.
//
// Printed at 1, 10, 1000 and every 100000 calls, so a log tail carries the
// evidence without flooding. Unconditional rather than env-gated: the whole
// point is that the flag-off arm can be compared against the flag-on arm, and
// a second env var would just be another thing to get wrong.
// ---------------------------------------------------------------------------
void Attend(const char* what, unsigned long long& n) {
    ++n;
    if (n == 1 || n == 10 || n == 1000 || (n % 100000) == 0)
        printf("[GFXBODY] %s calls=%llu\n", what, n);
}

unsigned long long g_fcibNewCalls = 0;
unsigned long long g_matNewCalls  = 0;

// ?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z, retail 0x1800c7a50, is
// System::MemoryAllocator::MemAlloc, called directly (B3R).

// ?SetupShaders@VideoProgram@Video@@QEBAXAEAVContext@2@@Z, retail 0x1800f4520.
//
// THE "exported and unported" VERDICT ABOVE IS STALE (ENDGAME B.3.5 caller
// batch). `Video::VideoProgram::SetupShaders` is a DEFINED member today —
// src/engine/video/video_program.cpp:178, which takes the context as the same
// `void*` this door passed — so the by-name door, its `Fn` typedef, its
// function-local cache and its readiness test are GONE and this is a member
// call. The sentence about growing the cutover surface was true when written and
// is answered: the surface it named is ours now.
// B.4 (R-GFX): typed; `Video::VideoProgram::SetupShaders` takes retail's
// `Context&`. The program is read unguarded, as retail does.
void SetupShaders(const ::Video::VideoProgram* program, Context& ctx) {
    program->SetupShaders(ctx);
}

}  // namespace

// ============================================================================
// ??2FrameCopyImageBuffer@Graphics@@SAPEAX_K@Z   retail 0x18001efc0, ord 787
// ??2MaterialInstance@Graphics@@SAPEAX_K@Z       retail 0x180002820, ord 823
// ============================================================================
// Two class `operator new`s. Both bodies are TWENTY-FOUR INSTRUCTIONS and, once
// the rel32 and RIP-relative fields are discounted, they are the same body as
// each other and the same body as the two already ported at
// src/engine/video/context_ctor.cpp:200 (`??2ImageBuffer@Video@@`,
// `??2RenderBuffer@Video@@`):
//
//     cs = *(void**)0x1801fc718 + 0x640;      // MemoryAllocator singleton lock
//     EnterCriticalSection(cs);               // IAT 0x18014b390
//     p = MemoryAllocator::Allocate(*(void**)0x1801fc718, bytes, 8);
//     LeaveCriticalSection(cs);               // IAT 0x18014b388
//     return p;
//
// ALIGNMENT IS THE WHOLE DIFFERENCE IN THIS FAMILY, AND IT WAS CHECKED BY BODY,
// NOT BY RESEMBLANCE — context_ctor.cpp:192 makes exactly that point about the
// sibling at 0x180011640, which is the same nineteen instructions with
// `MOV R8D,0x4`. Both bodies here disassemble to `MOV R8D,0x8`, at 0x18001eff4
// and 0x180002854 respectively. Read the two immediates; do not infer them.
//
// WHY ROUTING THROUGH MemAlloc IS THE FAITHFUL SPELLING AND NOT A SHORTCUT:
// `?MemAlloc@MemoryAllocator@System@@SAPEAX_KH@Z` (0x1800c7a50) was
// disassembled for this lane and is instruction-for-instruction the same body
// with the alignment arriving in EDX instead of being baked into the immediate
// — same singleton, same +0x640 critical section, same
// `Allocate(singleton, bytes, align)`, same return path. Calling it with 8 is
// not an approximation of these two functions; it is them.
// ============================================================================
extern "C" void* RtGraphics_FrameCopyImageBuffer_OperatorNew(uint64_t bytes) {
    Attend("FrameCopyImageBuffer::operator new", g_fcibNewCalls);
    return System::MemoryAllocator::MemAlloc(bytes, 8);                          // MOV R8D,0x8 @ 0x18001eff4
}

extern "C" void* RtGraphics_MaterialInstance_OperatorNew(uint64_t bytes) {
    Attend("MaterialInstance::operator new", g_matNewCalls);
    return System::MemoryAllocator::MemAlloc(bytes, 8);                          // MOV R8D,0x8 @ 0x180002854
}

// ============================================================================
// ?Setup@MaterialInstance@Graphics@@QEBAXAEAVContext@Video@@H@Z
// retail 0x180102c90, ordinal 4028. 332 bytes, 83 instructions, ONE call.
// ============================================================================
// Binds one shader's worth of this material's textures and constants onto a
// Video::Context. `QEBA` — it is CONST, and the disassembly agrees: every write
// lands on the Context, never on `this`.
//
// Three phases:
//
//   1. `SetupShaders` on the VideoProgram this material's Material holds for
//      the requested shader index. The only call in the body, and the only
//      external dependency.
//   2. TEXTURES. Walk `parameters[shader].textures`; for each binding, look up
//      `textures[sourceIndex].image` and, IF IT DIFFERS from what the Context
//      already has in that slot, store it and raise two dirty bits.
//   3. CONSTANTS. Walk `parameters[shader].constants`; for each, compare the
//      float4 at `constants[sourceIndex].value` against the Context's material
//      staging slot and, IF ANY COMPONENT DIFFERS, copy all four and raise the
//      material-staging dirty bit.
//
// FOUR TRANSCRIPTION POINTS WORTH NAMING, because each is a place a plausible
// C++ rewrite silently diverges:
//
//  (a) BOTH LOOPS RE-READ THEIR BOUNDS AND THEIR DATA POINTER EVERY ITERATION.
//      Retail reloads `_data` at the top (`MOV RDX,[RDI]` at 0x180102cd0,
//      `MOV RCX,[RDI+0x10]` at 0x180102d20) and the count at the bottom
//      (`CMP [RDI+8],R9D`, `CMP [RDI+0x18],R10D`). Nothing in either body can
//      change them, so hoisting would be equivalent — it is written unhoisted
//      anyway, because "equivalent under an assumption I checked once" is how a
//      port drifts when the assumption later stops holding.
//
//  (b) THE SHIFT IS MASKED. `MOV EAX,1; SHL EAX,CL` — an x86 32-bit shift
//      masks the count to 5 bits, so slot 32 sets bit 0, not nothing. The
//      `& 31` below is that mask, not defensive programming; without it C++
//      would be UNDEFINED where retail is defined.
//
//  (c) THE FLOAT COMPARE IS UCOMISS + JP + JNE, i.e. UNORDERED COUNTS AS
//      DIFFERENT. Plain C++ `==` on float is false for NaN, which is the same
//      verdict, so `!(a == b)` is faithful here. Do not "improve" it to a
//      bitwise compare: -0.0 and +0.0 compare EQUAL under ucomiss and differ
//      bitwise, and retail takes the equal path.
//
//  (d) THE COPY IS INTEGER, NOT FLOAT. Retail moves the four values with
//      `MOV EAX,[..]` / `MOV [..],EAX`, so a signalling NaN or an unused
//      payload passes through bit-exact. std::memcpy reproduces that; a float
//      assignment would not be guaranteed to.
//
// ORACLE: NONE, AND THIS IS NOT REPLAYABLE. The body's entire effect is a
// mutation of a LIVE Video::Context, and it is deliberately NOT idempotent —
// the second run of a differential pair would find every value already stored
// and take the equal path, raising none of the dirty bits the first run raised.
// A "clean" diff would therefore be an artefact of replay, not agreement
// (reference_calls_zero_can_mean_always_took_the_uncovered_branch.md is the
// same shape one step further on). Its verification tier is stated honestly in
// the lane doc and is not VERIFIED.
// ============================================================================

// ============================================================================
// Registration.
//
// NAMES ONLY, NO ADDRESSES, AND THAT IS A DECISION RATHER THAN AN OMISSION.
// Lane GFXBODY's own triage found a row in this same namespace
// (`?AddDrawItems@MeshInstance@`) where a RegisterRuntimeAddr would be ACTIVE
// HARM because src/diag/scene_watch.cpp:400 uses the retail address as an
// IDENTITY TOKEN against a live vtable slot. That is a reason to check every
// address before registering it, not a reason to register none — so each of the
// three was checked:
//
//   0x18001efc0, 0x180002820  — zero occurrences anywhere in code/ engine/
//                               runtime/ standalone/ src/render/dx11/. No door, no token.
//   0x180102c90               — occurs only in COMMENTS (three of them, in
//                               src/engine/ui/spritecontext_flush.cpp at :95,
//                               :138 and :260). Both live consumers,
//                               src/game/Player/
//                               tool_base_tilerender.cpp:100 and
//                               src/engine/ui/spritecontext_flush.cpp:267,
//                               resolve it BY NAME.
//
// So a name registration reaches every consumer that exists and an address
// registration would reach none that a name does not. Adding one anyway would
// be three lines of dead code that a future reader has to re-derive the safety
// of; see reference_count_the_doors_not_the_calls.md for why the count that
// matters is doors, not calls.
//
// Each mangled name is repeated as its own STRING LITERAL rather than via a
// named constant: tools/runtime_census.py matches `RegisterRuntimeExport("<lit>"`
// and cannot see through a constant, which is how Wave 3's Object count read
// 21/41 when it was 27/41
// (reference_census_cannot_see_nonliteral_registrations.md).
// ============================================================================

// ############################################################################
// ENDGAME B.3.5 step 5, block GC — `Graphics::MaterialInstance::Setup`'s body,
// whose live door is `RtGraphics_MaterialInstance_Setup` above.
//
// Its own `namespace Graphics { }` block, because a member definition has to
// appear in a namespace ENCLOSING the class and `Graphics` / `RtGraphics` are
// siblings (B34_BRIEFS A.2). This file's helpers sit in the GLOBAL unnamed
// namespace, so unqualified lookup from here DOES reach them (the reachable half
// of the A.2 trap; wave28_lane_gfxbody.cpp:957 records the other half).
//
// The transcription notes (a)-(d) in the door's banner above are unchanged and
// still describe this body — only the spelling of the receiver changed.
//
// TWO RAW OFFSET READS BECAME MEMBER CHAINS, which is item 6 of this block:
// `Field<void*>(self, 0x28)` is `_material` (an `Object::ExternalPtr<Material>`
// held BY VALUE) and `Field<void**>(mat, 0x28)` is that `Material`'s `_passes`
// array data pointer. Retail's `MOV RAX,[RCX+0x28] ; MOV RAX,[RAX+0x28] ;
// MOV RCX,[RAX+RDI*8]` is now spelled with the three names the two real classes
// give those bytes, and `Material.h`'s own `static_assert`s are what keep the
// spelling and the offsets welded together.
// ############################################################################

namespace Graphics {

// EXT SEAM (NOT RETAIL, docs/extensions/BLOSSOM.md): null unless natural tree growth is on (Material.h).
void (*MaterialInstance::_extAfterSetup)(const MaterialInstance& instance, Video::Context& context) = nullptr;

void MaterialInstance::Setup(Video::Context& context, int32_t shader) const {
    // prog = _material->_passes._data[shader]   (MOV RAX,[RCX+0x28] ;
    //                                            MOV RAX,[RAX+0x28] ;
    //                                            MOV RCX,[RAX+RDI*8])
    SetupShaders(_material.Get()->_passes._data[shader].Get(), context);

    // set = _parameters._data + shader*0x20  (SHL RDI,5 ; ADD RDI,[RSI+0x30])
    const RtGraphics::MaterialParameterSet* set = _parameters._data + shader;

    // ---- phase 2: textures ------------------------------------------------
    for (int32_t i = 0; i < set->textures._size; ++i) {
        const RtGraphics::MaterialBinding& b = set->textures._data[i];
        const Video::ImageBuffer* image = _textures._data[b.sourceIndex].image;
        int32_t slot = b.shaderSlot;
        if (context._imageBuffers[slot] != image) {
            context._dirty            |= kDirtyImageBuffersBit;
            context._imageBufferDirty |= (1u << (static_cast<uint32_t>(slot) & 31u));  // (b)
            context._imageBuffers[slot] = image;
        }
    }

    // ---- phase 3: constants -----------------------------------------------
    for (int32_t i = 0; i < set->constants._size; ++i) {
        const RtGraphics::MaterialBinding& b = set->constants._data[i];
        const float* src = _constants._data[b.sourceIndex].value;
        // ctx +0x208 + slot * 0x10, stepped flat as retail does (no bound on
        // the slot, so not `material[b.shaderSlot]`).
        float* dst = &context._pixelConstantStaging.material[0][0] + b.shaderSlot * 4;

        // (c) unordered counts as different, and all four are compared before
        // any is written — retail bails to the copy on the FIRST mismatch and
        // then copies all four, which is the same set of stores.
        if (dst[0] == src[0] && dst[1] == src[1] &&
            dst[2] == src[2] && dst[3] == src[3]) {
            continue;
        }
        std::memcpy(dst, src, 4 * sizeof(float));       // (d) integer moves
        context._dirty |= kDirtyMaterialStagingBit;
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/BLOSSOM.md): the blossom code for this draw; null = retail.
    if (_extAfterSetup != nullptr) {
        _extAfterSetup(*this, context);
    }
}

}  // namespace Graphics
