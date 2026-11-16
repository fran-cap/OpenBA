#pragma once
// ============================================================================
// src/engine/video/ShaderProgram.h — `Video::ShaderProgram` and its two
// concrete subclasses, `Video::VertexProgram` and `Video::PixelProgram`, as
// REAL C++ CLASSES (ENDGAME B.3.5 block B, B35_RULES 2.1/2.2, section 10 move 4).
// ============================================================================
//
// Every offset is pinned by a named instruction in RETAIL Runtime-x64.dll
// (image base 0x180000000), read out of `objdump -d` over the shipped file —
// never out of Ghidra's decompiled C. The layout below is UNCHANGED
// from the POD this header carried before B.3.5: the three asserted offsets and
// the two sizes are the same numbers, now on a class with a base and a vtable.
//
// ---------------------------------------------------------------------------
// Video::ShaderProgram : Object::CompiledExternal   (sizeof 0x50, 8 slots)
// ---------------------------------------------------------------------------
// ??0ShaderProgram@Video@@QEAA@XZ (0x1800f5030) stamps four vtables in
// sequence — ??_7Base@Object@@6B@ (0x180154788), ??_7External@Object@@6B@
// (0x1801547a0), ??_7CompiledExternal@Object@@6B@ (0x180154a30) and finally
// ??_7ShaderProgram@Video@@6B@ (0x180157b60) — zeroes +0x08/+0x10/+0x18
// (External's Filename and Array) and then six more qwords, +0x20 .. +0x48.
// It never touches +0x50, and the vector deleting destructor at 0x18004e360
// carries `MOV EDX,0x50`, so sizeof is exactly 0x50. THE THIRD STAMP IS THE
// PROOF OF THE BASE: `?_ParentTypeName@ShaderProgram@Video@@CAPEBDXZ`
// (0x18004e090) returns "Object::CompiledExternal" and the ctor stamps that
// table, which `Video::VideoProgram`'s ctor does NOT (see VideoProgram.h).
//
//   +0x20  Array<Parameter>        _textures    (GetTextureCount  0x18004e410
//                                                reads +0x28; GetTextureName
//                                                0x18004e420 indexes +0x20 by
//                                                SHL 4; GetTextureDest
//                                                0x18004e470 reads elem+0x08;
//                                                IsTextureInternal 0x18004e480
//                                                reads elem+0x0c)
//   +0x30  Array<Parameter>        _constants   (GetConstantCount 0x18004e4a0
//                                                reads +0x38; GetConstantName
//                                                0x18004e4b0 indexes +0x30;
//                                                GetConstantDest  0x18004e500
//                                                reads elem+0x08)
//   +0x40  Array<Array<uint8_t>>   _byteCode    (GetByteCode 0x18004e100 is
//                                                `(Array<E>*)(+0x40) + idx*0x10`
//                                                and IsValid 0x18004e0f0 is
//                                                `+0x48 > 0`)
//
// ---------------------------------------------------------------------------
// Video::VertexProgram / Video::PixelProgram : ShaderProgram  (0x58, 9 slots)
// ---------------------------------------------------------------------------
// Each adds ONE pointer at +0x50 — the platform companion object — and nothing
// else. ??0VertexProgram (0x1800f6100) and ??0PixelProgram (0x1800f3950) are
// the ShaderProgram ctor inlined, followed by the class's own vtable stamp
// (0x180157b10 / 0x180157ac0) and an inlined PlatformConstruct that asks
// Video::Device::_singleton->deviceData for the companion through a DIFFERENT
// DeviceData vtable slot per class:
//     VertexProgram  slot +0x78   (CreateVertexProgramData)
//     PixelProgram   slot +0x80   (CreatePixelProgramData)
// ?GetData@VertexProgram@Video@@QEBAPEBVVertexProgramData@2@XZ (0x18004e720) is
// `MOV RAX,[RCX+0x50]; RET`, which names +0x50 in the export table itself.
// Both deleting destructors (0x18004e810, 0x18004eb90) carry `MOV EDX,0x58`.
//
// THE TWO ARE SEPARATE CLASSES, not one class and two typedefs (which is what
// this header carried before B.3.5). Their LAYOUT is identical; their vtable,
// their `_typeInfo`, their `_FileExtension` and the DeviceData slot their
// PlatformConstruct calls are not, and retail's `_ParentTypeName` for both is
// the literal "ShaderProgram" (0x18004e530 / 0x18004e8b0).
//
// ============================================================================
// ENDGAME B.3 step 9, block VID (B39_RULES.md D1/D2)
// ============================================================================
// VertexProgram and PixelProgram are CONCRETE: retail's real public
// constructors (??0VertexProgram 0x1800f6100 / ??0PixelProgram 0x1800f3950),
// their own slot 0, their factory pairs as their own statics, so every live
// shader wears the COMPILER's table. ShaderProgram stays ABSTRACT in both
// retail's sense and C++'s (slots 3/6/7 are `_purecall`) with a PROTECTED real
// constructor (??0ShaderProgram 0x1800f5030) and its own concrete slot 0
// (0x18004e360). The three minted rows, their RT_DELDTOR rows, the ??0 / ??1
// doors and the slot doors are retired.
//
// THE DESTRUCTOR IS VIRTUAL (B3X W1): retail slot 0 is
// the deleting destructor; `~ShaderProgram()` is D1 + D0 here.
// The destructor CHAIN is retail's teardown, and that is not a coincidence:
// ~VertexProgram (0x1800f61b0) is ~ShaderProgram (0x1800f50b0) inlined followed
// by ~External (0x1800bc950), which is precisely what GCC emits for
// `VertexProgram : ShaderProgram : CompiledExternal : External`.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "../collection/Collection.h"
#include "../object/External.h"
#include "../object/Internal.h"                // Object::detail::InternalMemFree
#include "../system/String.h"

namespace rtcoll {
// B.4 wave Y (RT-GFX): the row of `Array<ShaderProgram::Parameter>`
// (src/engine/collection/array.cpp, SetSize 0x180055db0).
extern const ArraySpec kSpec_ShaderParam;
}

namespace IO { class Serializer; }   // io/Serializer.h

namespace Video {

class VertexProgramData;   // src/engine/video/CompanionData.h
class PixelProgramData;

// ============================================================
// Video::ShaderProgram — a compiled shader and its parameter bindings.
// ============================================================
class ShaderProgram : public Object::CompiledExternal {
public:
    // ========================================================
    // Video::ShaderProgram::Parameter — 0x10 bytes, a `struct` (the mangled
    // `??0Parameter@ShaderProgram@Video@@QEAA@AEBU012@@Z` spells `U`, not `V`).
    // Named by ??0Parameter@ShaderProgram@Video@@QEAA@VString@System@@H_N@Z
    // (0x1800f4f30), whose (String, int, bool) argument list is exactly the
    // three fields the Serialize loop inside 0x1800f5110 touches:
    // String::Serialize at elem+0x00, ReaderWriter::Int at elem+0x08 and, when
    // GetVersion() >= 1, Bool at elem+0x0c. The last three bytes are padding
    // and are never written.
    //
    // Retail exports five special members for it --
    //     ??0Parameter…@XZ          0x1800f4fa0   default ctor
    //     ??0Parameter…@VString@System@@H_N@Z  0x1800f4f30
    //     ??0Parameter…@AEBU012@@Z  0x18004e150   copy ctor
    //     ??4Parameter…@AEBU012@@Z  0x18004e190   assignment
    //     ??1Parameter…@XZ          0x18004e110   destructor
    // B.4 wave Y (RT-GFX): `name` is a System::String, so the element is no
    // longer trivial. Every Parameter lives in `_textures` / `_constants`, whose
    // type names retail's row `kSpec_ShaderParam` (SetSize 0x180055db0: the
    // String adopt, the zeroing init; destroy 0x18004fed0, the String release),
    // and ~ShaderProgram calls the registered ~Array 0x18004fb00 on both, which
    // zeroes each header, so the members' implicit ~Array that follows releases
    // nothing. No Parameter is built, copied or destroyed through C++ outside
    // those rows, so each name is released once.
    // ========================================================
    struct Parameter {
        System::String name;  // +0x00  (B.4 wave Y: the String; the rows above own its lifecycle)
        int32_t dest;      // +0x08  the register/sampler index this binds to
        bool    internal;  // +0x0c  only serialized when GetVersion() >= 1 (B.4 R-VIDEO: bool, H4)
        uint8_t _pad0d[3]; // +0x0d  never written by any arm

        // ?Serialize@Parameter@ShaderProgram@Video@@QEAAXAEAVSerializer@IO@@@Z
        // 0x1800f4ed0 — not ported; `ShaderProgram::Serialize` inlines it, which
        // is what retail's 0x1800f5110 does too.
        void Serialize(void* serializer);
    };

    // ---- the eight slots, in measured order -----------------------------
    // Slots 3 (`GetFileVersion`), 6 (`Reconstruct`) and 7 (`Destruct`) are
    // `_purecall` in retail's table and are NOT redeclared: `Object::External`
    // already declares all three `= 0`, so they stay pure and this class stays
    // abstract in retail's sense as well as C++'s. Slot 4 is
    // `Object::External::Compile`, INHERITED (retail's slot 4 is 0x1800066e0,
    // External's own body).
    //
    // [0] 0x18004e360 -- MSVC's VECTOR deleting destructor (`MOV EDX,0x50`, the
    // eh-vector dtor over ~ShaderProgram 0x1800f50b0), both arms as the retired
    // RT_DELDTOR row ran them. Reached only on a ShaderProgram-typed object,
    // which nothing builds (the class is abstract); each subclass has its own.
    // ??1ShaderProgram@Video@@UEAA@XZ (0x1800f50b0). Virtual (B3X W1); also the
    // subclasses' base destructor.
    ~ShaderProgram() override;
    // [1] 0x18004e0c0 TypeName / [2] 0x18004e0a0 Type, TypeId 0x18004e0b0, `_TypeName` / `_ParentTypeName` and the factory pair:
    // retail's macro (ENDGAME B.3.9 TI-REST, B39_RULES D6); the record is
    // `DEFINE_TYPE`d in src/engine/video/video_type_records.cpp.
    DECLARE_TYPE(ShaderProgram)
public:
    bool          Serialize(IO::Stream& stream) override;           // [5] 0x1800f5110

    // ---- the exported non-virtual members ------------------------------
    // Every `?…@ShaderProgram@Video@@` in the shipped export table is declared
    // here, ported or not, so no later block has to edit this header.
    bool    IsValid() const;                                     // 0x18004e0f0
    const Collection::Array<uint8_t>& GetByteCode(int32_t index) const;  // 0x18004e100
    int32_t GetTextureCount() const;                             // 0x18004e410
    int32_t GetTextureDest(int32_t index) const;                  // 0x18004e470
    bool    IsTextureInternal(int32_t index) const;               // 0x18004e480
    int32_t GetConstantCount() const;                             // 0x18004e4a0
    int32_t GetConstantDest(int32_t index) const;                 // 0x18004e500

    // The two hidden-return-slot members (B35_RULES 2.5, RULE B5-12). `?AV` in
    // `?AVString@System@@` is the by-VALUE spelling, and retail's bodies confirm it
    // by READING RDX three times (0x18004e4b0: `MOV [RSP+0x10],RDX`, `MOV [RDX],R9`,
    // `MOV RAX,RDX`) — settled from the prologue, not from the mangling.
    //
    // NEITHER IS A VIRTUAL, which is what makes declaring them by value safe: RULE
    // B5-12 as amended records that GCC's ms_abi lowering passes the hidden return
    // pointer BEFORE `this` while MSVC/retail passes `this` first, so a retail
    // VTABLE SLOT that returns by value must be declared `T* M(T* ret, …)` in retail
    // order. These two are called as plain members, by
    // `Graphics::Material::GetConstantName` / `GetTextureName`
    // (src/engine/graphics/wave28_lane_gfxbody.cpp). The analysis is in
    // `src/engine/video/shader_program_names.cpp`.
    System::String GetTextureName(int32_t index) const;           // 0x18004e420
    System::String GetConstantName(int32_t index) const;          // 0x18004e4b0

    // ?PlatformSerialize@ShaderProgram@Video@@QEAAXAEAVSerializer@IO@@@Z
    // 0x1800f52b0 — PUBLIC (`QEAA`), and called by both subclasses' Serialize.
    void PlatformSerialize(IO::Serializer& serializer);

    // `?CreateNewObject@` / `?CreateInPlaceObject@` exist for every external
    // (declared by DECLARE_TYPE); ShaderProgram's are the ABSTRACT class's:
    // `XOR EAX,EAX ; RET` both, 0x18004e0d0 / 0x18004e0e0 (B39_RULES D2 -- the
    // class's own statics, shaderprogram.cpp).

    // ??0ShaderProgram@Video@@QEAA@AEBV01@@Z (0x18004e200) and
    // ??4ShaderProgram@Video@@QEAAAEAV01@AEBV01@@Z (0x18004e290) are REAL,
    // EXPORTED bodies, not compiler defaults — the situation RULE B5-10 records
    // for `Graphics::Geometry`. Declared, and deliberately left undefined:
    // nothing in the tree calls either, and `= default` would silently
    // substitute a bitwise copy for whatever they do with the three arrays and
    // the parameter names.
    ShaderProgram(const ShaderProgram& other);                    // 0x18004e200 — not ported
    ShaderProgram& operator=(const ShaderProgram& other);         // 0x18004e290 — not ported


    // `_TypeName` 0x18004e080 -> "ShaderProgram", `_ParentTypeName` 0x18004e090
    // -> "Object::CompiledExternal": DECLARE_TYPE's (private, as retail's `CA`).

protected:
    // ??0ShaderProgram@Video@@QEAA@XZ 0x1800f5030 -- the REAL constructor,
    // PROTECTED on the abstract base. CompiledExternal() (Base's, External's and
    // CompiledExternal's vptrs, External's zero stores), ShaderProgram's vptr
    // (the compiler's), then the three Array headers +0x20..+0x4f zeroed by their
    // own constructors -- retail's six qword stores. +0x50 is never touched.
    ShaderProgram() : Object::CompiledExternal(), _textures(), _constants(), _byteCode() {}


public:
    // ---- the 0x30 bytes ShaderProgram adds ------------------------------
    // PUBLIC because `src/render/dx11/` reads `_byteCode` directly — the same posture
    // `Object::External` takes with `_path` / `_refs`, and what lets
    // `src/render/dx11/shaderprograms.cpp` drop its `ShaderProgramOwner` view.
    Collection::Array<Parameter, Collection::DefaultAllocator,
                      Collection::ExplicitSpec<&rtcoll::kSpec_ShaderParam>> _textures;   // +0x20
    Collection::Array<Parameter, Collection::DefaultAllocator,
                      Collection::ExplicitSpec<&rtcoll::kSpec_ShaderParam>> _constants;  // +0x30
    Collection::Array<Collection::Array<uint8_t>> _byteCode;   // +0x40
};

static_assert(sizeof(ShaderProgram) == 0x50,
              "Video::ShaderProgram is 0x50 (MOV EDX,0x50 at 0x18004e392)");
static_assert(alignof(ShaderProgram) == 8, "Video::ShaderProgram is qword-aligned");
static_assert(offsetof(ShaderProgram, _textures)  == 0x20, "ShaderProgram::_textures @ +0x20");
static_assert(offsetof(ShaderProgram, _constants) == 0x30, "ShaderProgram::_constants @ +0x30");
static_assert(offsetof(ShaderProgram, _byteCode)  == 0x40, "ShaderProgram::_byteCode @ +0x40");

static_assert(sizeof(ShaderProgram::Parameter) == 0x10,
              "ShaderProgram::Parameter is 0x10 (SHL RAX,0x4 at 0x18004e103)");
static_assert(offsetof(ShaderProgram::Parameter, name) == 0x0, "Parameter::name");
static_assert(offsetof(ShaderProgram::Parameter, _pad0d) == 0xD, "Parameter::_pad0d");
static_assert(alignof(ShaderProgram::Parameter) == 8, "Parameter is qword-aligned");
static_assert(offsetof(ShaderProgram::Parameter, dest) == 0x08,
              "Parameter::dest @ +0x08 (MOV EAX,[RAX+R8*8+0x8] at 0x18004e47a)");
static_assert(offsetof(ShaderProgram::Parameter, internal) == 0x0c,
              "Parameter::internal @ +0x0c (MOVZX EAX,[RAX+R8*8+0xc] at 0x18004e48a)");
// B.4 wave Y: the element carries a String (see the Parameter banner); the
// arrays' row, not the trait-derived spec, owns its lifecycle.
static_assert(!std::is_trivially_destructible<ShaderProgram::Parameter>::value,
              "ShaderProgram::Parameter::name is a System::String");

// ============================================================
// Video::VertexProgram — the compiled vertex shader. 0x58, 9 slots.
// ============================================================
class VertexProgram : public ShaderProgram {
public:
    // ??0VertexProgram@Video@@QEAA@XZ 0x1800f6100 -- the REAL constructor: ShaderProgram()
    // inlined (five vptr stamps in all, the compiler's), this class's own vptr,
    // then PlatformConstruct fills +0x50.
    VertexProgram() : ShaderProgram() {
        PlatformConstruct();   // +0x50
    }

    // Slot 4 is `Object::External::Compile`, inherited through ShaderProgram.
    //
    // [0] 0x18004e810 -- MSVC's VECTOR deleting destructor (`MOV EDX,0x58`, the
    // eh-vector dtor over ~VertexProgram 0x1800f61b0), both arms as the retired
    // RT_DELDTOR row ran them. The scalar arm's class-specific operator delete is
    // the locked MemFree. Nothing `new[]`s one.
    // ??1VertexProgram@Video@@UEAA@XZ 0x1800f61b0. Virtual (B3X W1). reached through slot 0.
    // ~ShaderProgram / ~CompiledExternal / ~External are the chain.
    ~VertexProgram() override;
    // [1] 0x18004e560 TypeName / [2] 0x18004e540 Type, TypeId 0x18004e550, `_TypeName` / `_ParentTypeName` and the factory pair:
    // retail's macro (ENDGAME B.3.9 TI-REST, B39_RULES D6); the record is
    // `DEFINE_TYPE`d in src/engine/video/video_type_records.cpp.
    DECLARE_TYPE(VertexProgram)
public:
    int32_t           GetFileVersion() const override;            // [3] 0x1800f6230 — `MOV EAX,5`
    bool              Serialize(IO::Stream& stream) override;           // [5] 0x1800f6240
    Object::External* Reconstruct() override;                     // [6] 0x18004e6e0
    void              Destruct() override;                        // [7] 0x18004e710
    // [8] 0x18004e700. `virtual`, NOT `override`: `Object::External`'s and
    // `CompiledExternal`'s retail tables are eight entries, so this branch
    // introduces `Construct` itself (RULE B5-16).
    virtual Object::External* Construct();

    // ?GetData@VertexProgram@Video@@QEBAPEBVVertexProgramData@2@XZ  0x18004e720
    const void* GetData() const;
    // ?Bind@VertexProgram@Video@@QEBAXAEAVContext@2@H@Z  0x1800f6660
    void Bind(void* context, int32_t permutation) const;

    // Retail's factory pair 0x18004e650 / 0x18004e6d0 (B39_RULES D2; declared
    // by DECLARE_TYPE), the class's own statics. The class declares its own
    // placement `operator new` (not ported): `::new (p)`.
    static void*         operator new(size_t size);               // 0x18004e570 — not ported
    static void*         operator new(size_t size, void* storage);// 0x18004e5e0 — not ported
    static void          operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                // 0x18004e5f0 — MemFree

    // Real exported bodies, not compiler defaults; see ShaderProgram's pair.
    VertexProgram(const VertexProgram& other);                    // 0x18004e730 — not ported
    VertexProgram& operator=(const VertexProgram& other);         // 0x18004e780 — not ported

    // The three platform halves. `AEAA` is PRIVATE IN RETAIL, not unexported;
    // they are public here.
    void PlatformConstruct();    // 0x1800f65e0
    void PlatformDestruct();     // 0x1800f6620
    bool PlatformCreate();       // 0x1800f6640 — returns the companion's slot 1

    // `_TypeName` 0x18004e520 -> "VertexProgram", `_ParentTypeName` 0x18004e530
    // -> "ShaderProgram": DECLARE_TYPE's.
    static const char* _FileExtension();   // 0x18004e510 -> "srsl"

public:
    VertexProgramData* _data;   // +0x50  from DeviceData slot 15 (+0x78); B.4 R-VIDEO, H4

    // ---- the version-0 vertex-stream records (B.4 R-GFX) --------------------
    // Moved here from src/engine/video/vertexprogram.cpp so the collection
    // bodies that build and destroy them (array.cpp `kSpec_VtxElement18` /
    // `kSpec_VtxStream18`, array_destroy.cpp 0x180056960 / 0x180056cb0) can
    // name them. Layouts from retail's two SetSize instances 0x1800567d0
    // (stride 0x18 {String, String, uint, int}) and 0x180056af0 (stride 0x18
    // {Array<Element>, String}), and from the destroy 0x180056cb0 (the String
    // at +0x10 released first, then the nested array's elements, then its
    // buffer). The nested arrays are unrolled `Collection::Array<T>` triples
    // handed by address to their unexported template instances (H5: an `Array`
    // member would run its own destructor on top of the explicit teardown).
    // Field names are vertexprogram.cpp's (the v0 Serialize reads them in this
    // order); none is a retail symbol.
    struct Element {                 // stride 0x18
        System::String name;           // +0x00
        System::String semantic;       // +0x08
        uint32_t       format;         // +0x10
        int32_t        index;          // +0x14
    };
    struct ElementArray { Element* data; int32_t size; int32_t capacity; };
    struct Stream {                  // stride 0x18
        ElementArray   elements;       // +0x00
        System::String name;           // +0x10
    };
};

static_assert(sizeof(VertexProgram::Element) == 0x18, "v0 vertex element is 0x18 bytes (0x1800567d0)");
static_assert(offsetof(VertexProgram::Element, name)     == 0x00, "Element::name @ +0x00");
static_assert(offsetof(VertexProgram::Element, semantic) == 0x08, "Element::semantic @ +0x08");
static_assert(offsetof(VertexProgram::Element, format)   == 0x10, "Element::format @ +0x10");
static_assert(offsetof(VertexProgram::Element, index)    == 0x14, "Element::index @ +0x14");
static_assert(sizeof(VertexProgram::ElementArray) == 0x10, "unrolled Array header");
static_assert(offsetof(VertexProgram::ElementArray, data) == 0x0, "ElementArray::data");
static_assert(offsetof(VertexProgram::ElementArray, size) == 0x8, "ElementArray::size");
static_assert(offsetof(VertexProgram::ElementArray, capacity) == 0xC, "ElementArray::capacity");
static_assert(sizeof(VertexProgram::Stream) == 0x18, "v0 vertex stream is 0x18 bytes (0x180056af0)");
static_assert(offsetof(VertexProgram::Stream, elements) == 0x00, "Stream::elements @ +0x00");
static_assert(offsetof(VertexProgram::Stream, name)     == 0x10, "Stream::name @ +0x10 (0x180056cb0 releases it first)");

static_assert(sizeof(VertexProgram) == 0x58,
              "Video::VertexProgram is 0x58 (MOV EDX,0x58 at 0x18004e82f)");
static_assert(alignof(VertexProgram) == 8, "Video::VertexProgram is qword-aligned");
static_assert(offsetof(VertexProgram, _data) == 0x50,
              "VertexProgram::_data @ +0x50 (MOV RAX,[RCX+0x50] — ?GetData@VertexProgram@Video@@)");

// ============================================================
// Video::PixelProgram — the compiled pixel shader. 0x58, 9 slots. Same layout
// as VertexProgram, different vtable / typeinfo / DeviceData slot (+0x80).
// ============================================================
class PixelProgram : public ShaderProgram {
public:
    // ??0PixelProgram@Video@@QEAA@XZ 0x1800f3950 -- the REAL constructor: ShaderProgram()
    // inlined (five vptr stamps in all, the compiler's), this class's own vptr,
    // then PlatformConstruct fills +0x50.
    PixelProgram() : ShaderProgram() {
        PlatformConstruct();   // +0x50
    }

    // Slot 4 is `Object::External::Compile`, inherited through ShaderProgram.
    //
    // [0] 0x18004eb90 -- MSVC's VECTOR deleting destructor (`MOV EDX,0x58`, the
    // eh-vector dtor over ~PixelProgram 0x1800f3a00), both arms as the retired
    // RT_DELDTOR row ran them. The scalar arm's class-specific operator delete is
    // the locked MemFree. Nothing `new[]`s one.
    // ??1PixelProgram@Video@@UEAA@XZ 0x1800f3a00. Virtual (B3X W1). reached through slot 0.
    // ~ShaderProgram / ~CompiledExternal / ~External are the chain.
    ~PixelProgram() override;
    // [1] 0x18004e8e0 TypeName / [2] 0x18004e8c0 Type, TypeId 0x18004e8d0, `_TypeName` / `_ParentTypeName` and the factory pair:
    // retail's macro (ENDGAME B.3.9 TI-REST, B39_RULES D6); the record is
    // `DEFINE_TYPE`d in src/engine/video/video_type_records.cpp.
    DECLARE_TYPE(PixelProgram)
public:
    int32_t           GetFileVersion() const override;            // [3] 0x1800f3a80 — `MOV EAX,5`
    bool              Serialize(IO::Stream& stream) override;           // [5] 0x1800f3a90
    Object::External* Reconstruct() override;                     // [6] 0x18004ea60
    void              Destruct() override;                        // [7] 0x18004ea90
    virtual Object::External* Construct();                        // [8] 0x18004ea80 (RULE B5-16)

    // ?GetData@PixelProgram@Video@@QEBAPEBVPixelProgramData@2@XZ  0x18004eaa0
    const void* GetData() const;
    // ?Bind@PixelProgram@Video@@QEBAXAEAVContext@2@H@Z  0x1800f3ed0
    void Bind(void* context, int32_t permutation) const;

    // Retail's factory pair 0x18004e9d0 / 0x18004ea50 (B39_RULES D2; declared
    // by DECLARE_TYPE), the class's own statics. The class declares its own
    // placement `operator new` (not ported): `::new (p)`.
    static void*         operator new(size_t size);               // 0x18004e8f0 — not ported
    static void*         operator new(size_t size, void* storage);// 0x18004e960 — not ported
    static void          operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                // 0x18004e970 — inline

    PixelProgram(const PixelProgram& other);                      // 0x18004eab0 — not ported
    PixelProgram& operator=(const PixelProgram& other);           // 0x18004eb00 — not ported

    void PlatformConstruct();    // 0x1800f3e40
    void PlatformDestruct();     // 0x1800f3e90
    bool PlatformCreate();       // 0x1800f3eb0

    // `_TypeName` 0x18004e8a0 -> "PixelProgram", `_ParentTypeName` 0x18004e8b0
    // -> "ShaderProgram": DECLARE_TYPE's.
    static const char* _FileExtension();   // 0x18004e890 -> "srsl"

    // The element of `Collection::Array<Video::PixelProgram::Sampler>`
    // (SetSize 0x180054a20, retail's own label string): stride 0x10, align 8.
    // The version-0 serializer (0x1800f3a90's legacy block) reads it as
    // String / SerializeInt / three SerializeBool; the array's assign arm
    // (0x180054ac4..0x180054aef) copies +0x00 (String ref), +0x08 (dword),
    // +0x0c / +0x0d / +0x0e (bytes); +0x0f is written by neither arm.
    struct Sampler {
        System::String name;    // +0x00
        int32_t        unit;    // +0x08
        bool           flagA;   // +0x0c
        bool           flagB;   // +0x0d
        bool           flagC;   // +0x0e
    };

public:
    PixelProgramData* _data;    // +0x50  from DeviceData slot 16 (+0x80); B.4 R-VIDEO, H4
};

static_assert(sizeof(PixelProgram::Sampler) == 0x10, "PixelProgram::Sampler is 0x10 (SHL RCX,0x4 at 0x180054a71)");
static_assert(offsetof(PixelProgram::Sampler, name)  == 0x00, "PixelProgram::Sampler::name @ +0x00");
static_assert(offsetof(PixelProgram::Sampler, unit)  == 0x08, "PixelProgram::Sampler::unit @ +0x08");
static_assert(offsetof(PixelProgram::Sampler, flagA) == 0x0c, "PixelProgram::Sampler::flagA @ +0x0c");
static_assert(offsetof(PixelProgram::Sampler, flagB) == 0x0d, "PixelProgram::Sampler::flagB @ +0x0d");
static_assert(offsetof(PixelProgram::Sampler, flagC) == 0x0e, "PixelProgram::Sampler::flagC @ +0x0e");

static_assert(sizeof(PixelProgram) == 0x58,
              "Video::PixelProgram is 0x58 (MOV EDX,0x58 at 0x18004ebaf)");
static_assert(alignof(PixelProgram) == 8, "Video::PixelProgram is qword-aligned");
static_assert(offsetof(PixelProgram, _data) == 0x50,
              "PixelProgram::_data @ +0x50 (MOV RAX,[RCX+0x50] — ?GetData@PixelProgram@Video@@)");

}  // namespace Video

// ============================================================================
// The `rt::video` spellings this header used to DECLARE are now ALIASES of the
// real classes — ONE definition of the layout, not two (BLOCK_BRIEF "the
// shape": "where class and POD are field-identical prefer ONE definition and an
// alias over a duplicate layout"). B4b-1 does not fire: `rt::video` is a sibling
// of `Video`, so the POD never occupied the class's own name.
//
// `ShaderProgramWithData` is GONE rather than aliased: it was ONE struct
// standing for two classes, and there is no single class it can name now.
// Nothing outside this header ever used the name.
//
// The `devicedata_vtbl` namespace below is not an alias.
// ============================================================================
namespace rt {
namespace video {

using ShaderParameter = ::Video::ShaderProgram::Parameter;
using ShaderProgram   = ::Video::ShaderProgram;
using VertexProgram   = ::Video::VertexProgram;
using PixelProgram    = ::Video::PixelProgram;

// DeviceData vtable slots that make the platform companion. Same object, two
// different slots; getting these two swapped produces a PixelProgramData behind
// a VertexProgram's vtable, which fails only on the first Bind.
namespace devicedata_vtbl {
    constexpr size_t kCreateVertexProgramData = 0x78;  // CALL [RAX+0x78] @0x1800f6192
    constexpr size_t kCreatePixelProgramData  = 0x80;  // CALL [RAX+0x80] @0x1800f39e2
    constexpr size_t kCreateRenderStateData   = 0x88;  // CALL [RAX+0x88] @0x1800f4c0d
}

// (B5 CLOSE: the `programdata_vtbl` byte-offset constants that stood here had no
// reader; the companion's slots are the `Video::{Vertex,Pixel}ProgramData` /
// `RenderStateData` virtuals in CompanionData.h.)

} // namespace video
} // namespace rt
