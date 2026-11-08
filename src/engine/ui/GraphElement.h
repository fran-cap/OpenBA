#pragma once
// UI::GraphElement -- offsets used by Wave 7 lane E.
//
// Evidence (Runtime-x64.dll addresses):
//   RemoveGraphs()      0x180040610  tail-calls FUN_1800416d0(this+0xf0):
//                                    destroys every element of the Array at
//                                    this+0xf0, frees its buffer under the
//                                    allocator lock, zeroes {data,size,cap}.
//                                    NOT an exported name -- called by address
//                                    (ResolveAddr), same idiom as
//                                    src/engine/graphics/scene.cpp's
//                                    RtArrayPtrSetSize / RtFreeArrayPtr.
//   AddGraph(color)     0x180040620  PORTED by Wave 9 lane UIW. Grows the
//                                    graph-series Array at this+0xf0 (element
//                                    stride 0x18) by one via 0x1800417d0 -- now
//                                    ours too (src/engine/collection/array.cpp,
//                                    `kSpec_GraphSeries`) -- then writes `color`
//                                    into the new element's first 4 bytes.
//                                    Wave 7 lane E and Wave 9 lane UIF both held
//                                    it back because 0x1800417d0 was unported;
//                                    that is what changed, not the reading.
//   AddGraphPoint(g,v)  0x180040690  PORTED by Wave 9 lane UIF (the hold-back
//                                    below is the ORIGINAL Wave 7 note, kept
//                                    because its mechanism was right; what
//                                    changed is only that 0x180023830 became
//                                    ours in lane Q, so the row now closes).
//                                    Appends to a per-series
//                                    Array<int32_t> living inside graph series
//                                    `g` at (seriesArray[g] + 0x08), via the
//                                    exported-by-address 0x180023830
//                                    (Array<uint32_t>::SetSize, the SAME
//                                    instantiation src/engine/graphics/
//                                    scene.cpp's RtArrayU32SetSize already
//                                    calls for Graphics::Scene -- shared
//                                    template instantiation, independent
//                                    confirmation of the address).

#include <cstdint>

namespace UI { namespace GraphElementOffsets {

constexpr int kGraphsData  = 0xF0;  // Collection::Array<GraphSeries> .data
constexpr int kGraphsCount = 0xF8;  // Collection::Array<GraphSeries> .size (int32_t)

// ---------------------------------------------------------------------------
// GraphSeries -- REVERSED by Wave 9 lane UIW (this header previously said
// "stride 0x18, NOT reversed -- only touched via the retail helper", which was
// true until AddGraph's element-assign had to be written out).
//
// Evidence: the assign arm of 0x1800417d0 at 0x180041890..0x1800418b1 writes
// exactly the colour dword and the nested Array header, and the destroy arm
// 0x1800419b0 destroys an Array at record+0x08 with stride 0x18.
//
//   +0x00  colour    uint32   AddGraph's argument; the ONLY 4 bytes AddGraph
//                             itself writes after the append.
//   +0x04  (hole)    4 bytes  NEVER WRITTEN BY ANY ARM. Not padding this lane
//                             chose -- padding retail leaves. It is declared as
//                             kSpec_GraphSeries's assign-unwritten window so no
//                             oracle compares it
//                             (reference_oracle_must_not_compare_unwritten_bytes.md).
//   +0x08  points    Collection::Array<int32_t>  data +0x08 / size +0x10 /
//                             capacity +0x14. This is the array
//                             GraphElement::AddGraphPoint appends into
//                             (widgets_layout.cpp reads it as "record + 8"),
//                             reached there by the SAME arithmetic -- two
//                             independent bodies agreeing on the offset.
// ---------------------------------------------------------------------------
constexpr int kSeriesStride      = 0x18;
constexpr int kSeriesColour      = 0x00;
constexpr int kSeriesPointsArray = 0x08;

} } // namespace UI::GraphElementOffsets

// ############################################################################
// ENDGAME B.3 step 6 (block C5) — `UI::GraphElement`, the REAL C++ class
// ############################################################################
//
// PARENT, read (B36_SLOTS.md section 3):
//     ?_TypeName@GraphElement@UI@@CAPEBDXZ       @0x180040420 -> "GraphElement"
//     ?_ParentTypeName@GraphElement@UI@@CAPEBDXZ @0x180040430 -> "Element"
// Recorded, not used (RULE B6-7: no DEFINE_TYPE in step 6; slots 1/2 read the
// minted `?_typeInfo@GraphElement@UI@@0VBaseTypeInfo@Object@@A`).
//
// `??_7GraphElement@UI@@6B@` @ 0x1801569d0, TWELVE slots; sizeof 0x100
// (`MOV EDX,0x100` in the slot-0 vector deleting dtor 0x1800408b0):
//   [0] 0x1800408b0 deleting dtor (RT_DELDTOR row)  [1] 0x180040460 TypeName
//   [2] 0x180040440 Type          [3] Element::Persist (inherited)
//   [4] 0x1800d1810 ComputeSize   [5] 0x1800d16b0 ComputeLayout
//   [6] 0x1800d1360 Draw          [7..11] Element's (inherited)
//
// ABSTRACT (slot 0 `= 0`), NO real ctor: every live one is built by the
// `??0GraphElement@UI@@QEAA@XZ` door (0x1800d1880, factory_ui1.cpp) and wears
// the MINTED table. LAYOUT, every field a store in that ctor
// (factory_ui1_tables.inc `kGraphElement`, 0x1800d18a6..0x1800d1906) or a
// read in Draw; the names are the bodies' meanings, not recovered names.
//
// A RETAIL SLIP, read as disassembly this block: `?SetYDivisions@` (0x180040600)
// is `MOV [RCX+0xe4],EDX ; RET` — the SAME field `?SetXDivisions@`
// (0x1800405f0) writes. Draw reads +0xe4 for the vertical grid and +0xe8 for
// the horizontal one, so +0xe8 only ever holds the ctor's 4 (or a copy).
// Transcribed as retail has it.
// ############################################################################
#include <cstddef>
#include "Element.h"
#include "Sprite.h"
#include "../collection/Collection.h"

namespace UI {

// One series record of `_seriesData` (stride 0x18), named from the layout
// GraphElementOffsets documents above (B4).
struct GraphSeries {
    uint32_t colour;                        // +0x00  AddGraph's argument
    uint32_t pad04;                        // +0x04  NEVER WRITTEN BY ANY ARM
    Collection::Array<int32_t> points;      // +0x08  data / +0x10 size / +0x14 capacity
};
static_assert(sizeof(GraphSeries) == GraphElementOffsets::kSeriesStride, "");
static_assert(offsetof(GraphSeries, pad04) == 0x4, "GraphSeries::pad04");
static_assert(offsetof(GraphSeries, colour) == GraphElementOffsets::kSeriesColour, "");
static_assert(offsetof(GraphSeries, points) == GraphElementOffsets::kSeriesPointsArray, "");

class GraphElement : public Element {
public:
    // ??0GraphElement@UI@@QEAA@XZ 0x1800d1880 -- the REAL ctor (ENDGAME B.3.9
    // UI-B): `CALL ??0Element`, then retail's stores; the UI::Sprite member's
    // own ctor writes +0xd0 = 0 / +0xd8 = -1. +0xec is written by nothing.
    GraphElement() : Element() {
        _width          = 100;
        _height         = 100;
        _tickSize       = 4;
        _lineColor      = -1;
        _yRange         = 1.0f;
        _xDivisions     = 12;
        _yDivisions     = 4;
        _seriesData     = nullptr;
        _seriesCount    = 0;
        _seriesCapacity = 0;
    }

    // ---- the twelve slots, in retail order --------------------------------
    // [0] 0x1800408b0 -- MSVC's VECTOR deleting destructor (retail; here the
    // virtual destructor, B3X W1) (`MOV EDX,0x100` = the element size).
    // ??1GraphElement 0x1800d1840 -- VIRTUAL (B3X W1); retail's body in
    // graphelement.cpp, the compiler chains ~Element (retail's tail call).
    ~GraphElement() override;
    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180040460, Type 0x180040440, _TypeName 0x180040420, _ParentTypeName 0x180040430
    //         CreateNewObject 0x180040550, CreateInPlaceObject 0x1800405d0
    DECLARE_TYPE(GraphElement)
public:
    // [3] Persist — Element's
    void ComputeSize() override;                                  // [4] 0x1800d1810
    void ComputeLayout(int32_t x, int32_t y,
                       int32_t w, int32_t h) override;            // [5] 0x1800d16b0
    void Draw(SpriteContext& context, uint8_t alpha) const override; // [6] 0x1800d1360
    // [7] Update .. [11] GetBorder — Element's

    // ---- non-virtual members ----------------------------------------------
    // DEFINED in src/engine/ui/widgets_layout.cpp (moved there by the fragment
    // pass; the registered doors delegate to them).
    void RemoveGraphs();                                          // 0x180040610
    void AddGraph(uint32_t colour);                               // 0x180040620
    void AddGraphPoint(int32_t graph, int32_t value);             // 0x180040690
    // One instruction each, read as disassembly (see the banner for the slip).
    void SetYRange(float range)      { _yRange = range; }         // 0x1800405e0 MOVSS [RCX+0xe0],XMM1
    void SetXDivisions(int32_t n)    { _xDivisions = n; }         // 0x1800405f0 MOV [RCX+0xe4],EDX
    void SetYDivisions(int32_t n)    { _xDivisions = n; }         // 0x180040600 MOV [RCX+0xe4],EDX (sic)

    GraphElement(const GraphElement& src);    // 0x1800406d0  not ported (no caller)
    GraphElement& operator=(const GraphElement& src);             // 0x1800407a0  not ported

private:

public:
    int32_t  _width;          // +0xc0  ctor 100; ComputeSize -> _layoutWidth
    int32_t  _height;         // +0xc4  ctor 100; ComputeSize -> _layoutHeight
    int32_t  _tickSize;       // +0xc8  ctor 4; grid overhang outside the frame
    int32_t  _lineColor;      // +0xcc  ctor -1; CreateInstance copies it; no slot reads it
    ::Sprite _lineSprite;     // +0xd0  the 1x1 line sprite (index +0xd8, ctor -1)
    float    _yRange;         // +0xe0  ctor 1.0f; the value drawn at full height
    int32_t  _xDivisions;     // +0xe4  ctor 12; vertical grid lines
    int32_t  _yDivisions;     // +0xe8  ctor 4;  horizontal grid lines
    uint32_t _padEC;          // +0xec  written by nothing
    // +0xf0 Collection::Array<GraphSeries> (GraphElementOffsets above), kept
    // as its raw 16-byte header: the array's destroy and grow are addressed
    // helpers (0x1800416d0 / kSpec_GraphSeries), not a C++ ~Array.
    GraphSeries* _seriesData; // +0xf0  (B4 P2 H4: typed, stride 0x18)
    int32_t  _seriesCount;    // +0xf8
    int32_t  _seriesCapacity; // +0xfc
};

static_assert(sizeof(GraphElement) == 0x100,
              "UI::GraphElement is 0x100 (MOV EDX,0x100 in 0x1800408b0)");
static_assert(offsetof(GraphElement, _padEC) == 0xEC, "GraphElement::_padEC");
static_assert(offsetof(GraphElement, _seriesCapacity) == 0xFC, "GraphElement::_seriesCapacity");
static_assert(alignof(GraphElement) == 8, "UI::GraphElement aligns to 8");
static_assert(offsetof(GraphElement, _width)       == 0xc0, "");
static_assert(offsetof(GraphElement, _height)      == 0xc4, "");
static_assert(offsetof(GraphElement, _tickSize)    == 0xc8, "");
static_assert(offsetof(GraphElement, _lineColor)   == 0xcc, "");
static_assert(offsetof(GraphElement, _lineSprite)  == 0xd0, "");
static_assert(offsetof(GraphElement, _yRange)      == 0xe0, "");
static_assert(offsetof(GraphElement, _xDivisions)  == 0xe4, "");
static_assert(offsetof(GraphElement, _yDivisions)  == 0xe8, "");
static_assert(offsetof(GraphElement, _seriesData)  == GraphElementOffsets::kGraphsData, "");
static_assert(offsetof(GraphElement, _seriesCount) == GraphElementOffsets::kGraphsCount, "");

}  // namespace UI
