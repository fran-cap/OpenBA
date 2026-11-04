#pragma once
// ============================================================================
// src/engine/ui/SpriteCapture.h — UI::SpriteCapture, 0x58 bytes
// ============================================================================
//
// WAVE 10 LANE UI4. Recovered from the retail DISASSEMBLY of five bodies, not
// from Ghidra's decompiled C — the C output of `AddScissor` reads as if the
// coordinate record it builds were a dead local, and it is not. See the STRIDE
// note below, which is the single fact that decides the whole layout.
//
//   ??0SpriteCapture@UI@@QEAA@XZ        0x1800d9140  ctor
//   ?BeginAddingSprites@…@QEAAXXZ       0x1800d9010  clears +0x50 and both arrays
//   ?AddSprite@…@QEAAXAEBVSprite@2@HHHHMI@Z  0x1800d8b20
//   ?AddScissor@…@QEAAXHHHH_N@Z         0x1800d8ef0
//   ?GetWidth@…@QEBAHXZ                 0x1800d9220  MOV EAX,[RCX+0x50]; RET
//   ?GetHeight@…@QEBAHXZ                0x1800d9210  MOV EAX,[RCX+0x54]; RET
//
// A SpriteCapture is the recording buffer behind the dev-only sprite-capture
// tool (`SpriteCaptureUI`, exe 0x1400792b0). It records a PARALLEL PAIR of
// streams, one entry per drawn thing:
//
//   _sprites[i]  the UI::Sprite that was drawn (sheet + index)
//   _entries[i]  where and how it was drawn, plus which sheet it came from
//
// plus a de-duplicated list of the sheets seen, `_sheets`, which
// `_entries[i].sheetIndex` indexes. A SCISSOR is recorded in the same two
// streams with `_sprites[i] = Sprite{null, -1}` and a NEGATIVE sheetIndex
// (-1 = normal scissor, -2 = full-screen), which is how a reader tells the two
// kinds of entry apart.
//
// ---------------------------------------------------------------------------
// THE STRIDE FACT, and why Ghidra's C is misleading here
// ---------------------------------------------------------------------------
// Both AddSprite and AddScissor end in two calls:
//
//     CALL 0x18004bfc0   with RCX = this+0x40      ; Array<T>::SetSize(n, fill)
//     CALL 0x18004bde0   with RCX = this+0x30
//
// 0x18004bfc0 is already ported in src/engine/collection/array.cpp as
// `kSpec_Fill16` — stride 0x10 — so `this+0x40` is `Array<UI::Sprite>`.
// 0x18004bde0 computes its byte count as `LEA RCX,[RAX+RAX*4]; SHL RCX,3`
// (0x18004be38) = **element stride 0x28**, NOT 4.
//
// That number is what makes the 0x28-byte block each body builds on its stack
// the ELEMENT it appends, rather than the dead local it looks like in the
// decompiled C. Read as stride 4 the block is unreferenced and both functions
// throw away every coordinate they were handed; read as stride 0x28 it is the
// entry, and every field lands somewhere. The disassembly settles it and the
// decompiler does not — reference_ghidra_c_output_vs_disassembly.md.
//
// ---------------------------------------------------------------------------
// `Entry::name` IS OWNED BY THE ARRAY — and how that was nearly recorded wrong
// ---------------------------------------------------------------------------
// `Entry::name` is a System::String (a refcounted char*). Both bodies build
// the entry holding an OWNED reference, hand it to SetSize, and then release
// the local's reference on the way out. Read only that far, the net change is
// zero and the stored copy is held by nobody — which is exactly what this
// header said in its first draft, complete with a paragraph calling it a
// shipped defect that the port reproduces faithfully.
//
// It was WRONG, and what caught it was the probe's REFCOUNT LEDGER: the count
// on a shared name rose by +1 per implementation, symmetrically, where the
// theory predicted zero. 0x18004bde0 does not copy elements bitwise. Its
// element copier (0x18004be97..0x18004bed0) COPY-CONSTRUCTS:
//
//     MOV EAX,[RDX]         MOV [RCX],EAX            ; sheetIndex
//     MOV [RCX+0x8],R12                              ; name = null
//     MOV RAX,[RDX+0x8]     TEST RAX,RAX   JZ …
//     MOV [RCX+0x8],RAX     INC dword ptr [RAX-0x4]  ; ++refcount   <-- HERE
//     … +0x10 +0x14 +0x18 +0x1c +0x20 +0x24 as dwords …
//
// So the array element takes its own reference and retail's accounting is
// exactly right. Two things worth keeping from that: a "retail is buggy"
// conclusion deserves the same scepticism as any other (CLAUDE.md), and the
// instrument whose number disagreed with the theory was believed over the
// theory.
//
// That listing also CONFIRMS `pad04` from a second, independent witness: the
// copier writes +0x00, jumps to +0x08, and never touches +0x04..+0x07.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../object/External.h"
#include "../object/Internal.h"   // ENDGAME B.3.8 RB: InternalMemFree (the concrete slot 0)
#include "../collection/Collection.h" // B4 X7-RT: the three Array members
#include "Sprite.h"                   // B4 X7-RT: Array<Sprite> needs the complete element

// The hand-derived row of `Array<UI::SpriteCapture::CaptureEntry>` (array.cpp,
// 0x18004bde0): its destroy releases each entry's `name` String (0x18003f070),
// which is what retail's ~Array 0x18003e4a0 runs. Declared here so the member
// can name it.
namespace rtcoll {
extern const ArraySpec kSpec_CaptureEntry;
}

namespace UI {

class Sprite;
class SpriteContext;
class SpriteSheet;


// One recorded draw or scissor. Stride 0x28 — proved by 0x18004bde0's
// `LEA RCX,[RAX+RAX*4]; SHL RCX,3`, see the banner.
//
// Field offsets read off AddScissor's stores at 0x1800d8f82..0x1800d8f96 and
// AddSprite's at 0x1800d8caa..0x1800d8e54; the two agree.
struct CaptureEntry {
    int32_t sheetIndex;   // +0x00  index into _sheets, or -1/-2 for a scissor
    int32_t pad04;        // +0x04  never written by either body
    // B4 Y RT-UI: System::String. In `_entries` an entry is only built /
    // copied / destroyed by kSpec_CaptureEntry / kSpec_CaptureEntryZ's raw
    // arms (destroy 0x18003f070), which name `_data`. The two C++ objects are
    // AddScissor's / AddSprite's stack locals, whose name retail zeroes first
    // and releases as the last act of the body (0x1800d8fc2, AddSprite's
    // tail): this member's ctor and dtor.
    System::String name;  // +0x08  see THE DANGLING NAME above
    int32_t x1;           // +0x10
    int32_t y1;           // +0x14
    int32_t x2;           // +0x18
    int32_t y2;           // +0x1c
    float   rotation;     // +0x20
    uint32_t color;       // +0x24
};
static_assert(sizeof(CaptureEntry) == 0x28, "CaptureEntry must be 0x28 bytes");
static_assert(offsetof(CaptureEntry, pad04) == 0x4, "CaptureEntry::pad04");
static_assert(offsetof(CaptureEntry, sheetIndex) == 0x00, "");
static_assert(offsetof(CaptureEntry, name)       == 0x08, "");
static_assert(offsetof(CaptureEntry, x1)         == 0x10, "");
static_assert(offsetof(CaptureEntry, y1)         == 0x14, "");
static_assert(offsetof(CaptureEntry, x2)         == 0x18, "");
static_assert(offsetof(CaptureEntry, y2)         == 0x1c, "");
static_assert(offsetof(CaptureEntry, rotation)   == 0x20, "");
static_assert(offsetof(CaptureEntry, color)      == 0x24, "");

// `pad04` is the ONE window in a CaptureEntry that neither body writes: the
// stack block is filled field by field and +0x04 is never stored. An oracle
// that memcmp's two entries must skip it or it reports a divergence on
// whatever the two stacks happened to hold —
// reference_oracle_must_not_compare_unwritten_bytes.md.
inline constexpr size_t kCaptureEntryPadOff = 0x04;
inline constexpr size_t kCaptureEntryPadLen = 0x04;

// ============================================================================
// UI::SpriteCapture — 0x58 bytes, NINE slots, `Object::External`.
// ============================================================================
// ENDGAME B.3.6 block D: the POD that occupied the retail name here is now the
// real class (B4b-1: the POD had no users outside this block's files, so it is
// replaced rather than moved to RtUI::). Parent "Object::External"
// (`?_ParentTypeName@SpriteCapture@UI@@` 0x18003c7e0); sizeof 0x58 from the
// `MOV EDX,0x58` in its slot-0 deleting dtor (0x18003ccf0).
//
// `??_7SpriteCapture@UI@@6B@` @ 0x1801566a0 (B36_SLOTS.md section 1):
//   [0] +0x00 0x18003ccf0  (unexported) vector deleting dtor — the class's own (B.3.9)
//   [1] +0x08 0x18003c810  ?TypeName@SpriteCapture@UI@@UEBAPEBDXZ
//   [2] +0x10 0x18003c7f0  ?Type@SpriteCapture@UI@@UEBAIXZ
//   [3] +0x18 0x1800d9520  ?GetFileVersion@SpriteCapture@UI@@UEBAHXZ  (MOV EAX,1)
//   [4] +0x20 0x1800066e0  ?Compile@External@Object@@  INHERITED — NOT declared
//   [5] +0x28 0x1800d9530  ?Serialize@SpriteCapture@UI@@UEAA_NAEAVStream@IO@@@Z
//   [6] +0x30 0x18003ca40  ?Reconstruct@SpriteCapture@UI@@UEAAPEAVExternal@Object@@XZ
//   [7] +0x38 0x18003cb40  ?Destruct@SpriteCapture@UI@@UEAAXXZ
//   [8] +0x40 0x18003cac0  ?Construct@SpriteCapture@UI@@UEAAPEAVExternal@Object@@XZ — B6-1.1
//
// ENDGAME B.3 step 9 (block UI-D, B39_RULES D1/D2): every SpriteCapture wears
// the COMPILER's table. The real ctor (0x1800d9140) and dtor (0x1800d91b0) are
// the class's; the factory pair is the class's own (spritecapture_class.cpp),
// replacing the FACTUI2 row and its `Ctor_SpriteCapture` door; the minted
// `??_7SpriteCapture@UI@@6B@`, its RT_DELDTOR row and the `DoorBody` bridge
// are retired.
class SpriteCapture : public Object::External {
public:
    using CaptureEntry = ::UI::CaptureEntry;   // retail's nested spelling
                                               // (`Array<UI::SpriteCapture::CaptureEntry>`)

    // [0] CONCRETE (ENDGAME B.3.8 block RB, B38_RULES D1/D2): the exe
    // instantiates the class itself (its table copy 0x14015d550, stamped by
    // 0x1400792b0), whose slot 0 is the exe scalar deleting dtor 0x140013f80
    // (~SpriteCapture; MemFree on flags & 1) — the SCALAR arm is kept
    // (B39_RULES D1.2). The DLL's vector arm (0x18003ccf0, the retired
    // RT_DELDTOR row) differs only on `flags & 2`, which nothing passes.
    // 0x1800d91b0. VIRTUAL (B3X W1); the three ~Array calls (spritecapture_class.cpp),
    // then the compiler chains ~External.
    ~SpriteCapture() override;

    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x18003c810, Type 0x18003c7f0, CreateNewObject 0x18003c900, CreateInPlaceObject 0x18003c9c0
    DECLARE_TYPE(SpriteCapture)
public:
    int32_t     GetFileVersion() const override;        // [3] 0x1800d9520
    // [4] 0x1800066e0 is Object::External::Compile, inherited unchanged.
    bool        Serialize(IO::Stream& stream) override;       // [5] 0x1800d9530
    Object::External* Reconstruct() override;           // [6] 0x18003ca40
    void        Destruct() override;                    // [7] 0x18003cb40
    virtual Object::External* Construct();              // [8] 0x18003cac0 — B6-1.1

    // ---- ported non-virtual members ----------------------------------------
    // ?AddScissor@SpriteCapture@UI@@QEAAXHHHH_N@Z            0x1800d8ef0
    void AddScissor(int32_t x, int32_t y, int32_t right, int32_t bottom, bool fullScreen);
    // ?AddSprite@SpriteCapture@UI@@QEAAXAEBVSprite@2@HHHHMI@Z  0x1800d8b20
    void AddSprite(const Sprite& sprite, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                   float rotation, uint32_t color);
    // ?EndAddingSprites@SpriteCapture@UI@@QEAAXXZ           0x1800d8a60
    void EndAddingSprites();
    // ?BeginAddingSprites@SpriteCapture@UI@@QEAAXXZ         0x1800d9010 — read
    //   this pass: `+0x50 = 0 (qword)`, `SetSize(+0x40, 0)` (0x1800370c0),
    //   tail-jump `SetSize(+0x30, 0)` (0x18004ba90). SpriteContext::Begin
    //   inlines exactly these three; it now calls this member.
    void BeginAddingSprites();
    // ?Draw@SpriteCapture@UI@@QEBAXAEAVSpriteContext@2@HH@Z  0x1800d9230
    void Draw(SpriteContext& context, int32_t x, int32_t y) const;
    // ?GetWidth / ?GetHeight — 0x1800d9220 / 0x1800d9210, `MOV EAX,[RCX+0x50]` /
    // `[RCX+0x54]` (this header's banner).
    int32_t GetWidth() const  { return _width; }
    int32_t GetHeight() const { return _height; }

    // ---- exported, NOT PORTED, no caller: declared, deliberately undefined --
    SpriteCapture(const SpriteCapture& src);            // 0x18003cb50
    SpriteCapture& operator=(const SpriteCapture& src); // 0x18003cc20
    // 0x18003c800 — the minted record's type id (spritecapture_class.cpp).
    // Placement is spelt `::new (p) SpriteCapture()` (B9-5): these stay undefined.
    static void* operator new(std::size_t bytes);              // 0x18003c820
    static void* operator new(std::size_t bytes, void* place); // 0x18003c890
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                     // 0x18003c8a0
    // Retail's factory pair (B39_RULES D2): MemAlloc(0x58, 8) + the ctor, and
    // the null-tested in-place ctor. Defined and registered in
    // spritecapture_class.cpp.

    // ??0SpriteCapture@UI@@QEAA@XZ 0x1800d9140 — the REAL constructor, PUBLIC:
    // External() and the stamps are the compiler's; then the seven zero
    // stores +0x20..+0x50.
    // B4 X7-RT: the three Arrays are members, so their zero triples are their
    // own constructors, in declaration order (+0x20, +0x30, +0x40), then the
    // one qword at +0x50 written as the two named fields (H7).
    SpriteCapture() : Object::External(), _sheets(), _entries(), _sprites() {
        _width = 0; _height = 0;        // +0x50 (one qword)
    }

public:
    // ---- the 0x38 bytes SpriteCapture adds (public: the doors reach them) --
    // B4 X7-RT (`behaviour:`): typed members. Retail's ~SpriteCapture
    // 0x1800d91b0 calls ~Array on +0x40 (0x1800d91d4, 0x180037310), +0x30
    // (0x1800d91de, 0x18003e4a0: releases each entry's name) and +0x20
    // (0x1800d91e8, 0x18003da00), then ~External: reverse declaration order.
    Collection::Array<SpriteSheet*> _sheets;   // +0x20  Array<ExternalPtr<SpriteSheet>>, stride 8
    Collection::Array<CaptureEntry, Collection::DefaultAllocator,
                      Collection::ExplicitSpec<&rtcoll::kSpec_CaptureEntry>>
                                    _entries;  // +0x30  Array<CaptureEntry>,             stride 0x28
    Collection::Array<Sprite>       _sprites;  // +0x40  Array<UI::Sprite>,               stride 0x10
    int32_t      _width;      // +0x50  GetWidth
    int32_t      _height;     // +0x54  GetHeight

    // NOT RETAIL (review F-024): retail has no such check. Serialize's read arm calls this
    // after reading `_entries` and before the `_sprites` rebuild. A sheetIndex is serialized
    // apart from the sheet count. One at or past `_sheets._size` reads a pointer past the
    // array at 0x1800d968f, and any negative one other than the scissor sentinels -1 / -2
    // leaves a sprite entry with no sheet that Draw dereferences (0x1800d93fb). A capture
    // holding such an entry is malformed: all its entries are dropped, so it loads (and
    // draws) as an empty capture. Returns true when it dropped them. A valid capture's
    // entries are scissors or name a sheet slot and are left as they are.
    bool DropEntriesNamingNoSheet() {
        for (int32_t i = 0; i < _entries._size; ++i) {
            const int32_t sheetIndex = _entries._data[i].sheetIndex;
            if (sheetIndex == -1 || sheetIndex == -2)
                continue;   // a scissor: Draw's first two arms, no sheet read
            if (sheetIndex >= 0 && sheetIndex < _sheets._size)
                continue;
            _entries.SetSize(0);
            return true;
        }
        return false;
    }
};
static_assert(sizeof(SpriteCapture) == 0x58,
              "UI::SpriteCapture is 0x58 bytes (MOV EDX,0x58 in slot 0, 0x18003ccf0)");
static_assert(alignof(SpriteCapture) == 8, "UI::SpriteCapture aligns to 8 (the vptr)");
static_assert(offsetof(SpriteCapture, _sheets)  == 0x20, "_sheets at +0x20");
static_assert(offsetof(SpriteCapture, _entries) == 0x30, "_entries at +0x30");
static_assert(offsetof(SpriteCapture, _sprites) == 0x40, "_sprites at +0x40");
static_assert(offsetof(SpriteCapture, _width)   == 0x50, "GetWidth 0x1800d9220 reads +0x50");
static_assert(offsetof(SpriteCapture, _height)  == 0x54, "GetHeight 0x1800d9210 reads +0x54");
static_assert(offsetof(SpriteCapture, _sheets) == sizeof(Object::External),
              "SpriteCapture's own bytes begin right after Object::External");

}  // namespace UI

// ==== B3R W0-P0c: Runtime bodies reached by retail address ====
// ENDGAME B.3R (B3R_RULES R5): each body below is registered at the retail
// address in its trailing comment and read through the registry today. It is
// declared here, copied from its definition (linkage, namespace, types), so the
// reader can call it by name. Nothing calls it through this declaration yet.

// ---- ui/spritecapture_class.cpp
namespace IO { class Serializer; }
extern "C" void RtUi_SpriteCapture_SerializeEntries(IO::Serializer* ser, int count, UI::CaptureEntry* e);  // 0x18004c0d0
// ==== end B3R W0-P0c ====
