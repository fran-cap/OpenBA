#pragma once
// ============================================================================
// src/engine/video/VertexLayout.h — `Video::VertexLayout` as a real C++
// class, plus its nested `Element`.
//
// ENDGAME B.3.5 block A, RULE B5-2.2: this is the ONE header step 5 has to
// AUTHOR rather than port. Before this file, `Video::VertexLayout`'s layout
// existed in exactly two places, both partial and both private to one
// translation unit:
//
//   * `src/render/dx11/dx11_backend_access.h`'s `VertexLayoutOwner` /
//     `VertexLayoutElement` — four offsets, read out of VideoDX11's
//     `VertexLayoutData::PlatformCreate` (0x180003ff0);
//   * an anonymous-namespace `struct VertexLayout` / `VertexElement` inside
//     `src/engine/video/vertexlayout.cpp`.
//
// Both are now views of THIS declaration. Nothing else in the tree named the
// layout, so there is no `Rt*` POD to move (B4b-1 does not fire).
//
// THE BODIES DO NOT LIVE HERE AND DO NOT MOVE IN THIS BLOCK.
// `src/engine/video/vertexlayout.cpp` keeps its ten registered doors exactly
// as they are; ENDGAME B.3.5 block B (the program/shader family) owns that file
// and moves the bodies into the members declared below. This block's job is a
// COMPLETE and CORRECT declaration so block B, block D and the dx11 caller
// batch never have to edit it.
//
// EVIDENCE — all read this pass from the RETAIL `Runtime-x64.dll` (15290
// functions, image base 0x180000000), as DISASSEMBLY, not decompiled C:
//
//   sizeof 0x48        the `MOV EDX,0x48` in the slot-0 vector deleting dtor
//                      (B35_SLOTS.md, `Video::VertexLayout`)
//   parent             `?_ParentTypeName@VertexLayout@Video@@CAPEBDXZ`
//                      (0x18004dca0) returns "Object::CompiledExternal"
//   9 slots            `??_7VertexLayout@Video@@6B@` @ 0x180157ba0
//   fields             `??0VertexLayout@Video@@QEAA@XZ` @ 0x1800f5d50:
//                        1800f5d66  LEA [0x180154788]  ??_7Base
//                        1800f5d70  LEA [0x1801547a0]  ??_7External
//                        1800f5d7a  zero +0x08, +0x10, +0x18   (External's)
//                        1800f5d90  LEA [0x180154a30]  ??_7CompiledExternal
//                        1800f5d9a  LEA [0x180157ba0]  ??_7VertexLayout
//                        1800f5da4  zero +0x20, +0x28          (_elements)
//                        1800f5dac  zero +0x38, +0x40          (_signature)
//                        1800f5db4  _singleton -> Device+0x100 -> vt[0x70]
//                        1800f5dd6  store the result at +0x30   (_data)
//                      and `??1VertexLayout` @ 0x1800f5df0, which destroys
//                      +0x30 (slot 0, flags=1), then +0x38 as
//                      `Array<uint8>::~Array` (0x1800112a0), then +0x20 as
//                      `Array<Element>::~Array` (0x18004fa30), re-stamps
//                      ??_7CompiledExternal and tail-JMPs ~External
//                      (0x1800bc950). Nothing touches +0x2c or +0x44, which is
//                      what makes both of them an `Array<T>::_capacity`.
// ============================================================================
//
// *** THE ELEMENT FIELD MAP IN `src/engine/video/vertexlayout.cpp` IS WRONG,
// *** AND `dx11_backend_access.h`'s IS RIGHT.  (B.3.5 block A finding.)
//
// `vertexlayout.cpp`'s local `VertexElement` reads
//   +0x08 stream  +0x0c usage  +0x10 usageIndex  +0x14 format  +0x18 offset
// Retail says otherwise, on two independent reads:
//
//   `?Add@VertexLayout@Video@@QEAAXHHW4Usage@12@HW4Format@12@@Z` @ 0x1800f5a90
//   builds one Element in its own frame and appends it. The five parameters
//   land in declaration order:
//       1800f5aa6  MOV [RSP+0x30],EDX    arg1 (int)     -> Element +0x08
//       1800f5aaa  MOV [RSP+0x34],R8D    arg2 (int)     -> Element +0x0c
//       1800f5aaf  MOV [RSP+0x38],R9D    arg3 (Usage)   -> Element +0x10
//       1800f5ab4  MOV [RSP+0x3c],...    arg4 (int)     -> Element +0x14
//       1800f5abf  MOV [RSP+0x40],...    arg5 (Format)  -> Element +0x18
//   (RSP+0x28 is the Element's leading `String name`, left null: `Add` does not
//   name an element, which is consistent with the DX11 backend never reading a
//   semantic — see `src/render/dx11/vertexlayoutdata.cpp`.)
//
//   VideoDX11's `VertexLayoutData::PlatformCreate` @ 0x180003ff0 reads
//   +0x08 as the D3D11 InputSlot, +0x0c as the AlignedByteOffset and +0x18 as
//   the format index. Those three agree with `Add` exactly.
//
//   `?GetStreamCount@VertexLayout@Video@@QEBAHXZ` @ 0x1800f5a30 walks the
//   element array in 0x20 steps and takes `max(elem+0x08) + 1` — so +0x08 is
//   the STREAM, which both readings already agreed on.
//
// So `format` is at +0x18, not +0x14, and +0x0c is the byte OFFSET, not the
// usage. The mistake is inert today: `Serialize` (0x1800563c0) writes the six
// fields positionally through the ReaderWriter's Int/UInt slots, and
// vertexlayout.cpp's permutation happens to keep the same int/uint/int/uint/int
// sequence on the wire, so no save or `MeshLayout\*.rsc` is misread. It is a
// naming defect, not a data defect — but it is exactly the shape that turns
// into one the moment a member reads a field by name. The names below are the
// retail-proved ones; `vertexlayout.cpp` belongs to block B and is left alone.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "../collection/Collection.h"
#include "../object/External.h"
#include "../object/Internal.h"                // Object::detail::InternalMemFree
#include "../system/String.h"                  // Element::name

namespace rtcoll {
// B.4 wave Y (RT-GFX): the row of `Array<VertexLayout::Element>`
// (src/engine/collection/array.cpp, SetSize 0x180056240).
extern const ArraySpec kSpec_VertexElement;
}

namespace IO { class Stream; }

namespace Video {

class VertexLayoutData;   // src/engine/video/CompanionData.h (B.4 R-VIDEO)

// ============================================================
// Video::VertexLayout — a compiled vertex-buffer layout. 0x48 bytes,
// `Object::CompiledExternal` base (which adds no bytes over `External`'s 0x20).
//
// ENDGAME B.3 step 9, block VID (B39_RULES.md D1/D2): CONCRETE. Retail's real
// public constructor (??0VertexLayout 0x1800f5d50), its own slot 0, and the
// factory pair as its own statics: every live VertexLayout wears the COMPILER's
// table. The minted row, its RT_DELDTOR row 0x18004e000, the ??0 / ??1 doors and
// the seven slot doors are retired (tools/rt_mint_retired/vid.txt).
//
// THE DESTRUCTOR IS VIRTUAL (B3X W1): retail slot 0 is
// the deleting destructor. `~VertexLayout()` is D1 + D0 here.
// ============================================================
class VertexLayout : public Object::CompiledExternal {
public:
    // ---- the two 4-byte enums `?Add@` names ----------------------------
    // `W4Usage@12@` / `W4Format@12@` in the mangling, i.e. enums NESTED in
    // `Video::VertexLayout`. THE ENUMERATOR NAMES ARE NOT RECOVERABLE from the
    // binary, so — exactly as `Object::External::CompileResult` does for the
    // same reason (External.h) — they are spelled as the 32-bit integers they
    // are rather than as enums with invented names.
    //
    // `Format` is known to have nine valid values, 0..8, from the table
    // VideoDX11's `PlatformCreate` builds on its stack; the DXGI mapping is
    // transcribed in `src/render/dx11/vertexlayoutdata.cpp`. Retail range-checks
    // neither (FormatInRange below is the port's check, review S-043).
    using Usage  = int32_t;
    using Format = int32_t;

    // NOT RETAIL (UB, out-of-bounds index; review S-043): retail VideoDX11
    // 0x180003ff0 indexes its 9-entry DXGI table with the raw format of a
    // MeshLayout .rsc element and has no check. True for every retail format 0..8.
    static constexpr uint32_t kFormatCount = 9;
    static constexpr bool FormatInRange(Format format) {
        return static_cast<uint32_t>(format) < kFormatCount;
    }

    // ============================================================
    // Video::VertexLayout::Element — 0x20 bytes, a `struct` (the mangled
    // `?GetElement@VertexLayout@Video@@QEBAAEBUElement@12@H@Z` spells `U`,
    // not `V`). Field map and its proof: the banner at the top of this file.
    //
    // `name` is a `System::String` (B.4 wave Y, RT-GFX). Every Element lives in
    // `_elements`, whose type names retail's row `kSpec_VertexElement` (SetSize
    // 0x180056240: the String adopt, the zeroing init; destroy 0x18004fe20, the
    // String release); ~VertexLayout calls the registered ~Array 0x18004fa30,
    // which zeroes the header, so the member's implicit ~Array releases nothing.
    // No Element is built, copied or destroyed through C++ outside the row.
    // ============================================================
    struct Element {
        System::String name;  // +0x00  the semantic name (the row owns its lifecycle)
        int32_t  stream;      // +0x08  vertex stream / D3D11 InputSlot
        int32_t  offset;      // +0x0c  byte offset within the stream
        Usage    usage;       // +0x10  never read by the DX11 backend
        int32_t  usageIndex;  // +0x14  never read by the DX11 backend
        Format   format;      // +0x18  0..8, indexes VideoDX11's DXGI table
        uint32_t _pad1c;      // +0x1c  NOT WRITTEN by `Add` (0x1800f5a90)
    };

    // ??0VertexLayout@Video@@QEAA@XZ 0x1800f5d50 -- the REAL constructor.
    // CompiledExternal() (Base's, External's and CompiledExternal's vptrs,
    // External's zero stores), VertexLayout's vptr (the compiler's), the two
    // Array headers zeroed (their own constructors: +0x20 and +0x38), then
    // PlatformConstruct fills +0x30 -- retail's order.
    VertexLayout() : Object::CompiledExternal(), _elements(), _signature() {
        PlatformConstruct();   // +0x30
    }

    // ---- the nine slots, in measured order -----------------------------
    // Slot 4 is `Object::External::Compile`, INHERITED (retail's slot 4 is
    // 0x1800066e0, External's own body), so it is not redeclared here.
    //
    // [0] 0x18004e000 -- MSVC's VECTOR deleting destructor (`MOV EDX,0x48`, the
    // eh-vector dtor over ~VertexLayout 0x1800f5df0), both arms as the retired
    // RT_DELDTOR row ran them. Nothing `new[]`s one.
    // ??1VertexLayout@Video@@UEAA@XZ (0x1800f5df0). VIRTUAL
    // (B3X W1); reached through slot 0.
    ~VertexLayout() override;
    // [1] 0x18004dcd0 TypeName / [2] 0x18004dcb0 Type, TypeId 0x18004dcc0, `_TypeName` / `_ParentTypeName` and the factory pair:
    // retail's macro (ENDGAME B.3.9 TI-REST, B39_RULES D6); the record is
    // `DEFINE_TYPE`d in src/engine/video/video_type_records.cpp.
    DECLARE_TYPE(VertexLayout)
public:
    int32_t           GetFileVersion() const override;        // [3] 0x1800f5e60 — `MOV EAX,3`
    bool              Serialize(IO::Stream& stream) override;       // [5] 0x1800f5e70
    Object::External* Reconstruct() override;                 // [6] 0x18004de50
    void              Destruct() override;                    // [7] 0x18004de80
    // [8] 0x18004de70. `virtual`, NOT `override`: `Object::External`'s and
    // `CompiledExternal`'s retail tables are eight entries, so this branch
    // introduces `Construct` itself (RULE B5-16).
    virtual Object::External* Construct();

    // ---- the exported non-virtual members ------------------------------
    // Every `?…@VertexLayout@Video@@` in the shipped export table is declared
    // here, ported or not, so no later block has to edit this header
    // (BLOCK_BRIEF "the shape"). The ten that `src/engine/video/
    // vertexlayout.cpp` already implements as doors are marked `door`; the rest
    // are `// not ported` — nothing in the tree calls them, they exist only as
    // rows in `standalone/rt_export_addrs.inc`.
    int32_t        GetElementCount() const;                       // 0x18004de90 — not ported
    const Element& GetElement(int32_t index) const;               // 0x18004dea0 — not ported
    int32_t        GetStreamCount() const;                        // 0x1800f5a30 — not ported
    void           BeginAdd();                                    // 0x1800f5b20 — not ported
    void           Add(int32_t stream, int32_t offset, Usage usage,
                       int32_t usageIndex, Format format);        // 0x1800f5a90 — not ported
    void           EndAdd();                                      // 0x1800f5a70 — not ported
    void           Bind(void* context) const;                     // 0x1800f6080 — not ported
    bool           operator==(const VertexLayout& other) const;   // 0x1800f5b30 — not ported

    // `??0VertexLayout@Video@@QEAA@AEBV01@@Z` (0x18004deb0) and
    // `??4VertexLayout@Video@@QEAAAEAV01@AEBV01@@Z` (0x18004df30) are REAL,
    // EXPORTED bodies, not compiler defaults — the same situation RULE B5-10
    // records for `Graphics::Geometry`. Declared, and deliberately left
    // undefined in this block: nothing in the tree calls either, and a
    // `= default` here would silently substitute a bitwise copy for whatever
    // those two do with the element array and the String names.
    VertexLayout(const VertexLayout& other);                      // 0x18004deb0 — not ported
    VertexLayout& operator=(const VertexLayout& other);           // 0x18004df30 — not ported

    // The static reflection companions retail emits. TypeId (0x18004dcc0) is
    // The factory pair 0x18004ddc0 / 0x18004de40 (declared by DECLARE_TYPE) is
    // the class's own (D2), defined in vertexlayout.cpp. The class declares its
    // own placement `operator new` (not ported): placement spells `::new (p)`.
    static void*        operator new(size_t size);                // 0x18004dce0 — not ported
    static void*        operator new(size_t size, void* storage); // 0x18004dd50 — not ported
    static void         operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                 // 0x18004dd60 — class_operator_delete.cpp


    // The three platform halves (`AEAA`, i.e. PRIVATE in retail, not unexported —
    // reference_aeaa_is_private_not_unexported.md). `PlatformCreate` returns void,
    // unlike VertexProgram's and PixelProgram's, which return bool — 0x180003ff0
    // falls off its end with no return value set, so that is deliberate.
    void PlatformConstruct();                    // 0x1800f5f90
    void PlatformDestruct();                     // 0x1800f5fd0
    void PlatformCreate();                       // 0x1800f6060
    // 0x1800f5ff0 — declared, NOT ported: retail's `Serialize` INLINES it, and
    // this port inlines it at the same place, so a separate body would be a second
    // copy of four statements with nothing calling it. Nothing in the tree reaches
    // this address (it has no registration of its own, here or anywhere).
    void PlatformSerialize(void* serializer);

    // `_TypeName` 0x18004dc90 -> "VertexLayout", `_ParentTypeName` 0x18004dca0
    // -> "Object::CompiledExternal": DECLARE_TYPE / DEFINE_TYPE's.

public:
    // ---- the 0x28 bytes VertexLayout adds ------------------------------
    // Spelled with leading underscores to match `src/engine/video/
    // vertexlayout.cpp`'s local view, so block B's move is a rename of the
    // struct and nothing else. Public because `src/render/dx11/` reads `_elements` and
    // `_signature` directly — the same posture `Object::External` takes with
    // `_path` / `_refs`.
    Collection::Array<Element, Collection::DefaultAllocator,
                      Collection::ExplicitSpec<&rtcoll::kSpec_VertexElement>>
        _elements;   // +0x20  {data, _size +0x28, _capacity +0x2c}
    // The `Video::VertexLayoutData*` the Device factory hands back
    // (`DeviceData` vtable +0x70, slot 14 — `Dx11_CreateVertexLayoutData`).
    VertexLayoutData*          _data;       // +0x30  (B.4 R-VIDEO, H4)
    // The compiled vertex-shader INPUT SIGNATURE blob. Retail reaches it only
    // through `PlatformSerialize`, which is why the field is named for the
    // platform half; VideoDX11 hands it to `CreateInputLayout` as the signature
    // and aborts the whole of `PlatformCreate` when `_size` is 0.
    Collection::Array<uint8_t> _signature;  // +0x38  {data, _size +0x40, _capacity +0x44}
};

static_assert(sizeof(VertexLayout) == 0x48,
              "Video::VertexLayout is 0x48 bytes (MOV EDX,0x48 in the slot-0 "
              "vector deleting dtor)");
static_assert(alignof(VertexLayout) == 8, "Video::VertexLayout is qword-aligned");
static_assert(offsetof(VertexLayout, _elements) == 0x20,
              "VertexLayout::_elements @ +0x20 (ctor 0x1800f5da4, "
              "VideoDX11 0x180003ff0)");
static_assert(offsetof(VertexLayout, _elements) + offsetof(Collection::Array<VertexLayout::Element>, _size) == 0x28,
              "VertexLayout::_elements._size @ +0x28 (Serialize 0x1800f5ebf)");
static_assert(offsetof(VertexLayout, _data) == 0x30,
              "VertexLayout::_data @ +0x30 (ctor 0x1800f5dd6, dtor 0x1800f5e10)");
static_assert(offsetof(VertexLayout, _signature) == 0x38,
              "VertexLayout::_signature @ +0x38 (ctor 0x1800f5dac, "
              "VideoDX11 0x180003ff0)");
static_assert(offsetof(VertexLayout, _signature) + offsetof(Collection::Array<uint8_t>, _size) == 0x40,
              "VertexLayout::_signature._size @ +0x40 (Serialize 0x1800f5f06)");

static_assert(sizeof(VertexLayout::Element) == 0x20,
              "Video::VertexLayout::Element is 0x20 bytes (the 0x20 stride "
              "0x1800563c0 and 0x180003ff0 both walk in)");
static_assert(offsetof(VertexLayout::Element, _pad1c) == 0x1C, "Element::_pad1c");
static_assert(alignof(VertexLayout::Element) == 8, "Element is qword-aligned");
static_assert(offsetof(VertexLayout::Element, name)       == 0x00, "Element::name @ +0x00");
static_assert(offsetof(VertexLayout::Element, stream)     == 0x08, "Element::stream @ +0x08 (Add arg1, GetStreamCount 0x1800f5a30)");
static_assert(offsetof(VertexLayout::Element, offset)     == 0x0c, "Element::offset @ +0x0c (Add arg2, VideoDX11 AlignedByteOffset)");
static_assert(offsetof(VertexLayout::Element, usage)      == 0x10, "Element::usage @ +0x10 (Add arg3)");
static_assert(offsetof(VertexLayout::Element, usageIndex) == 0x14, "Element::usageIndex @ +0x14 (Add arg4)");
static_assert(offsetof(VertexLayout::Element, format)     == 0x18, "Element::format @ +0x18 (Add arg5, VideoDX11 format index)");

}  // namespace Video
