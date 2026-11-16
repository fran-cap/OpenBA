#pragma once
// ============================================================================
// src/engine/video/VideoProgram.h — `Video::VideoProgram` as a real C++
// class. AUTHORED, not ported: B35_RULES 13.3 lists this layout as one where the
// SIZE was verified and the FIELDS were not, so closing that gap from the retail
// DLL is part of ENDGAME B.3.5 block B (B35_RULES section 10 move 4, last).
// ============================================================================
//
// *** THE HEADLINE TRAP OF THIS FAMILY, AND THIS IS THE CLASS IT IS ABOUT. ***
// `VIDEO_GRAPHICS_INVENTORY.md` §2a called the Video program family "8-9 slots,
// all `External` subclasses — mechanical after one". There are THREE different
// parents in it: `ShaderProgram` and `VertexLayout` derive from
// `Object::CompiledExternal`, `VertexProgram` and `PixelProgram` derive from
// `ShaderProgram`, and `VideoProgram` derives from `Object::External` DIRECTLY.
// Converting it on ShaderProgram's pattern would give it a `CompiledExternal`
// vtable stamp it must not have.
//
// PROVED TWICE, from the RETAIL DLL, as DISASSEMBLY:
//   1. `?_ParentTypeName@VideoProgram@Video@@CAPEBDXZ` (0x18004ec20) returns the
//      literal "Object::External" (B35_SLOTS.md section 3).
//   2. `??0VideoProgram@Video@@QEAA@XZ` @ 0x1800f45b0 stamps THREE vtables, not
//      four — and the missing one is CompiledExternal's:
//        1800f45c6  LEA RAX,[0x180154788]   ??_7Base@Object@@6B@
//        1800f45cd  MOV [RCX],RAX
//        1800f45d0  LEA RAX,[0x1801547a0]   ??_7External@Object@@6B@
//        1800f45d7  MOV [RCX],RAX
//        1800f45da  LEA RAX,[RCX+0x08]      \  zero +0x08 (Filename),
//        1800f45e5  MOV [RAX],RCX            |  +0x10 and +0x18 (External's Array)
//        1800f45e8  MOV [RBX+0x10],RCX       |
//        1800f45ec  MOV [RBX+0x18],RCX      /
//        1800f45f0  LEA RAX,[0x180157a70]   ??_7VideoProgram@Video@@6B@
//        1800f45f7  MOV [RBX],RAX
//      `??_7CompiledExternal@Object@@6B@` (0x180154a30) never appears, where
//      `??0ShaderProgram` (0x1800f5030) and `??0VertexLayout` (0x1800f5d50) both
//      stamp it between External's and their own.
//
// ---------------------------------------------------------------------------
// LAYOUT — sizeof 0x48, nine slots, every field named by its own exported getter
// ---------------------------------------------------------------------------
// The four `ExternalPtr` fields are pinned by the four getters, each of which is
// four instructions and names exactly one offset (read with `objdump -d` over the
// shipped Runtime-x64.dll; `MOV %rax,(%rdx)` is the hidden return slot, RULE
// B5-12):
//
//   18004ee10  ?GetVertexLayout@   mov 0x20(%rcx),%rax; mov %rax,(%rdx); mov %rdx,%rax; ret
//   18004ee20  ?GetVertexProgram@  mov 0x28(%rcx),%rax; …
//   18004ee30  ?GetPixelProgram@   mov 0x30(%rcx),%rax; …
//   18004ee40  ?GetRenderState@    mov 0x38(%rcx),%rax; …
//   18004ee50  ?GetData@           mov 0x40(%rcx),%rax; ret        (no return slot)
//
// and the TYPE of each comes from that getter's own mangled return type —
// `?AV?$ExternalPtr@VVertexLayout@Video@@@Object@@` and its three twins — so the
// field names are retail's, not a reader's guess.
//
// THE SAME FIVE OFFSETS ARE PROVED A SECOND TIME, INDEPENDENTLY, and that is why
// this header is not resting on one read. The ctor above zeroes +0x20, +0x28,
// +0x30, +0x38 and then writes the platform companion to +0x40:
//        1800f460a  MOV RAX,[0x1801fe7a8]    ?_singleton@Device@Video@@
//        1800f4611  MOV R8,[RAX+0x100]       the active DeviceData
//        1800f4618  TEST R8,R8 / JE          (the DeviceData, not the Device)
//        1800f4626  CALL [RAX+0x90]          DeviceData slot 18, CreateVideoProgramData
//        1800f462f  MOV [RBX+0x40],RCX       _data
// and `?SetupShaders@VideoProgram@Video@@` (0x1800f4520,
// src/engine/video/video_program.cpp) reads +0x20/+0x28/+0x30/+0x38 and copies
// each into a DIFFERENT `Video::Context` slot under a DIFFERENT dirty bit —
// ctx+0x0b0 bit 0x004, ctx+0x1f0 bit 0x040, ctx+0x1f8 bit 0x080, ctx+0x1c8 bit
// 0x020 — which independently identifies WHICH field is which (that file's banner
// carries the byte-level read). A third agreement: `Serialize` (0x1800f46b0)
// serializes the four in +0x20..+0x38 order through four DIFFERENT per-T
// `ExternalPtr<T>::Serialize` instantiations, 0x180055090 / 0x1800542a0 /
// 0x180055380 / 0x180055670, and those same four addresses appear for
// `ExternalPtr<VertexLayout>` / `<VertexProgram>` / `<PixelProgram>` /
// `<RenderState>` elsewhere in the tree.
//
// sizeof 0x48: the `MOV EDX,0x48` in the slot-0 vector deleting destructor
// (0x18004efb0 — B35_SLOTS.md), agreeing with `?CreateNewObject@` (0x18004ed40),
// which allocates 0x48 with alignment 8.
//
// ---------------------------------------------------------------------------
// ENDGAME B.3 step 9, block VID (B39_RULES.md D1/D2): CONCRETE. Retail's real
// public constructor (??0VideoProgram 0x1800f45b0), its own slot 0, and the
// factory pair as its own statics: every live VideoProgram wears the COMPILER's
// table. The minted row, its RT_DELDTOR row 0x18004efb0, the ??0 / ??1 doors and
// the seven slot doors are retired.
// ---------------------------------------------------------------------------
// THE DESTRUCTOR IS VIRTUAL (B3X W1): retail slot 0 is
// the deleting destructor. `~VideoProgram()` (0x1800f4650) is D1 + D0 here, and
// its tail-JMP to `~External` (0x1800bc950) is the destructor CHAIN.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "../object/External.h"
#include "../object/Internal.h"                // Object::detail::InternalMemFree

namespace Video {

class VideoProgramData;   // src/engine/video/CompanionData.h (B.4 R-VIDEO)

// The four resources a VideoProgram binds. Forward declarations only — an
// `Object::ExternalPtr<T>` is one `T*` and needs nothing more, and keeping the
// declaration this thin is what lets `src/render/dx11/` and the caller batches include this
// header without dragging the whole Video tree in. `RenderState` is block C's
// class and `VertexLayout` / `VertexProgram` / `PixelProgram` are block B's; all
// four are `class`, which is what B4b-5 asks a forward declaration to match.
class VertexLayout;
class VertexProgram;
class PixelProgram;
class RenderState;

// ============================================================
// Video::VideoProgram — a vertex layout, a vertex shader, a pixel shader and a
// render state, bound to a Context as one unit. 0x48 bytes, nine slots,
// `Object::External` DIRECTLY (see the banner).
// ============================================================
class VideoProgram : public Object::External {
public:
    // ??0VideoProgram@Video@@QEAA@XZ 0x1800f45b0 -- the REAL constructor.
    // External() (Base's and External's vptrs, +0x08/+0x10/+0x18 zeroed), NO
    // CompiledExternal (see the banner), VideoProgram's vptr (the compiler's),
    // the four ExternalPtrs zeroed, then PlatformConstruct fills +0x40.
    VideoProgram() : Object::External() {
        _vertexLayout  = nullptr;   // +0x20
        _vertexProgram = nullptr;   // +0x28
        _pixelProgram  = nullptr;   // +0x30
        _renderState   = nullptr;   // +0x38
        PlatformConstruct();        // +0x40
    }

    // ---- the nine slots, in measured order (B35_SLOTS.md) ---------------
    // Slot 4 is `Object::External::Compile`, INHERITED (retail's slot 4 is
    // 0x1800066e0, External's own body).
    //
    // [0] 0x18004efb0 -- MSVC's VECTOR deleting destructor (`MOV EDX,0x48`, the
    // eh-vector dtor over ~VideoProgram 0x1800f4650), both arms as the retired
    // RT_DELDTOR row ran them. Nothing `new[]`s one.
    // ??1VideoProgram@Video@@UEAA@XZ (0x1800f4650). VIRTUAL
    // (B3X W1); reached through slot 0. Retail's body is
    // PlatformDestruct; its tail-JMP to ~External is the destructor chain.
    ~VideoProgram() override;
    // [1] 0x18004ec50 TypeName / [2] 0x18004ec30 Type, TypeId 0x18004ec40, `_TypeName` / `_ParentTypeName` and the factory pair:
    // retail's macro (ENDGAME B.3.9 TI-REST, B39_RULES D6); the record is
    // `DEFINE_TYPE`d in src/engine/video/video_type_records.cpp.
    DECLARE_TYPE(VideoProgram)
public:
    int32_t           GetFileVersion() const override;            // [3] 0x1800f46a0 — `MOV EAX,1`
    bool              Serialize(IO::Stream& stream) override;           // [5] 0x1800f46b0
    Object::External* Reconstruct() override;                     // [6] 0x18004edd0
    void              Destruct() override;                        // [7] 0x18004ee00
    // [8] 0x18004edf0. `virtual`, NOT `override`: `Object::External`'s retail
    // table is eight entries, so this branch introduces `Construct` itself
    // (RULE B5-16).
    virtual Object::External* Construct();

    // ---- the exported non-virtual members ------------------------------
    // Every `?…@VideoProgram@Video@@` in the shipped export directory is declared
    // here so no later block edits this header.
    //
    // THE FOUR GETTERS ARE HIDDEN-RETSLOT EXPORTS IN RETAIL AND ARE NOT IN OURS,
    // AND THAT ASYMMETRY IS THE ONE THING A CALLER BATCH MUST NOT MISS.
    //
    // *** SETTLED FROM THE RETAIL PROLOGUE, PER GETTER, NOT FROM THE MANGLING. ***
    // B35_RULES 2.5 lists all eighteen `ExternalPtr<T>` getters as retslot and that
    // has been doubted on the grounds that an 8-byte pointer wrapper should come
    // back in a register. For THESE FOUR the bytes decide it, and they are retslot:
    //     18004ee10  mov 0x20(%rcx),%rax    ; this->_vertexLayout
    //                mov %rax,(%rdx)        ; *** STORE THROUGH THE SLOT IN RDX
    //                mov %rdx,%rax          ; *** AND RETURN THE SLOT
    //                ret
    // and 0x18004ee20/0x18004ee30/0x18004ee40 are the same four instructions with
    // 0x28/0x30/0x38. A body with no return slot never touches RDX — which is what
    // `?GetData@` (0x18004ee50, `mov 0x40(%rcx),%rax; ret`) looks like, four bytes
    // away and returning a plain pointer. So retail's `ExternalPtr<T>` has a
    // user-declared copy constructor or destructor that MSVC treats as
    // non-trivial, and the parameter order is retail's: RCX = this, RDX = ret.
    //
    // OUR `Object::ExternalPtr<T>` is trivially copyable and eight bytes
    // (External.h asserts both and B.3.3 requires the triviality), so GCC returns
    // it in RAX with NO hidden slot. The bodies below exist so that the four field
    // names are load-bearing rather than commentary.
    Object::ExternalPtr<VertexLayout>  GetVertexLayout() const;    // 0x18004ee10
    Object::ExternalPtr<VertexProgram> GetVertexProgram() const;   // 0x18004ee20
    Object::ExternalPtr<PixelProgram>  GetPixelProgram() const;    // 0x18004ee30
    Object::ExternalPtr<RenderState>   GetRenderState() const;     // 0x18004ee40
    // ?GetData@VideoProgram@Video@@QEBAPEBVVideoProgramData@2@XZ  0x18004ee50 —
    // `MOV RAX,[RCX+0x40]; RET`, no return slot. `void*` rather than
    // `Video::VideoProgramData*` so this header needs no CompanionData.h.
    const void* GetData() const;

    // ?SetupShaders@VideoProgram@Video@@QEBAXAEAVContext@2@@Z  0x1800f4520.
    // B.4 (R-GFX): retail's `Context&` (`AEAVContext@2@`); its engine caller is
    // graphics/wave21_lane_gfxbody.cpp's MaterialInstance::Setup; src/ext/better_water
    // and src/ext/winter_ice call it too.
    void SetupShaders(class Context& context) const;

    // ?Bind@VideoProgram@Video@@QEBAXAEAVContext@2@H@Z  0x1800f4830 — not ported.
    // Nothing in the tree calls it; the four Context setters SetupShaders inlines
    // are how the engine actually binds a program.
    void Bind(void* context, int32_t permutation) const;

    // The three platform halves. `AEAA` is PRIVATE IN RETAIL, not unexported;
    // public here as the rest of
    // this family has them. `PlatformCreate` returns void, like VertexLayout's
    // and unlike VertexProgram's and PixelProgram's.
    void PlatformConstruct();    // 0x1800f47a0
    void PlatformDestruct();     // 0x1800f47f0
    void PlatformCreate();       // 0x1800f4810

    // Retail's factory pair 0x18004ed40 / 0x18004edc0 (B39_RULES D2; declared
    // by DECLARE_TYPE), defined in videoprogram.cpp. The class declares its own
    // placement `operator new` (not ported): placement spells `::new (p)`.
    static void*         operator new(size_t size);                // 0x18004ec60 — not ported
    static void*         operator new(size_t size, void* storage); // 0x18004ecd0 — not ported
    static void          operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                 // 0x18004ece0

    // Real exported bodies, not compiler defaults (the situation RULE B5-10
    // records for `Graphics::Geometry`): declared, deliberately left undefined.
    // Nothing calls either, and `= default` would silently substitute a bitwise
    // copy for whatever they do with the four references.
    VideoProgram(const VideoProgram& other);                       // 0x18004ee60 — not ported
    VideoProgram& operator=(const VideoProgram& other);            // 0x18004ef00 — not ported

    // `_TypeName` 0x18004ec10 -> "VideoProgram", `_ParentTypeName` 0x18004ec20
    // -> "Object::External": DECLARE_TYPE / DEFINE_TYPE's.

public:
    // ---- the 0x28 bytes VideoProgram adds ------------------------------
    // PUBLIC, the posture `Object::External` takes with `_path` / `_refs`: the
    // Serialize body hands each field's ADDRESS to the per-T `ExternalPtr<T>`
    // serializer, and `SetupShaders` reads all four.
    Object::ExternalPtr<VertexLayout>  _vertexLayout;    // +0x20
    Object::ExternalPtr<VertexProgram> _vertexProgram;   // +0x28
    Object::ExternalPtr<PixelProgram>  _pixelProgram;    // +0x30
    Object::ExternalPtr<RenderState>   _renderState;     // +0x38
    // The `Video::VideoProgramData*` the Device factory hands back (DeviceData
    // vtable +0x90, slot 18 — `DeviceData::CreateVideoProgramData`).
    VideoProgramData*                  _data;           // +0x40  (B.4 R-VIDEO, H4)
};

static_assert(sizeof(VideoProgram) == 0x48,
              "Video::VideoProgram is 0x48 bytes (MOV EDX,0x48 in the slot-0 "
              "vector deleting dtor 0x18004efb0; CreateNewObject allocates 0x48)");
static_assert(alignof(VideoProgram) == 8, "Video::VideoProgram is qword-aligned");
static_assert(offsetof(VideoProgram, _vertexLayout)  == 0x20,
              "VideoProgram::_vertexLayout @ +0x20 (?GetVertexLayout@ 0x18004ee10)");
static_assert(offsetof(VideoProgram, _vertexProgram) == 0x28,
              "VideoProgram::_vertexProgram @ +0x28 (?GetVertexProgram@ 0x18004ee20)");
static_assert(offsetof(VideoProgram, _pixelProgram)  == 0x30,
              "VideoProgram::_pixelProgram @ +0x30 (?GetPixelProgram@ 0x18004ee30)");
static_assert(offsetof(VideoProgram, _renderState)   == 0x38,
              "VideoProgram::_renderState @ +0x38 (?GetRenderState@ 0x18004ee40)");
static_assert(offsetof(VideoProgram, _data)          == 0x40,
              "VideoProgram::_data @ +0x40 (?GetData@ 0x18004ee50; ctor 0x1800f462f)");
// The four fields are one pointer each and nothing else — if this ever fails, the
// ctor's four zero stores and Serialize's four per-field calls no longer describe
// the same object.
static_assert(sizeof(Object::ExternalPtr<VertexLayout>) == 8,
              "ExternalPtr<T> is one pointer");

}  // namespace Video

// B.4 (R-VIDEO): RtVideo_VertexProgram_SerializeStreams (0x180056db0) is
// vertexprogram.cpp's own typed helper now; its one caller is that file.
