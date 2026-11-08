#pragma once
// UI::SpriteSheet -- a real class (ENDGAME B.3.6 block D) and UI::SpriteInfo.
// Runtime-x64.dll class, 0x58 bytes.
//
// Inheritance: Object::Base -> Object::External -> UI::SpriteSheet
//
// Decompiled from (Wave 7 lane F, 2026-09-07, Runtime-x64.dll, image base
// 0x180000000):
//   Default ctor:            0x18011d600 (??0SpriteSheet@UI@@QEAA@XZ)
//   Copy ctor:               0x1800035d0 (??0SpriteSheet@UI@@QEAA@AEBV01@@Z)  -- not read this wave
//   Dtor:                    0x18011d670 (??1SpriteSheet@UI@@UEAA@XZ)         -- not read this wave
//   GetSpriteCount:          0x180003190 (?GetSpriteCount@SpriteSheet@UI@@QEBAHXZ)
//   GetSpriteIndexFromName:  0x1800031f0 (?GetSpriteIndexFromName@SpriteSheet@UI@@QEBAHAEBVString@System@@@Z)
//   GetUVs:                  0x180003330 (?GetUVs@SpriteSheet@UI@@QEBAXHAEAM000@Z)
//   AddSpriteInfo:           0x180003390 (?AddSpriteInfo@SpriteSheet@UI@@QEAAXAEBVString@System@@0HHHH@Z)
//   SetSpriteInfo:           0x180003460 (?SetSpriteInfo@SpriteSheet@UI@@QEAAXHHHMMMM@Z)
//   GetSpriteNameFromIndex:  0x1800032d0 (?GetSpriteNameFromIndex@SpriteSheet@UI@@QEBA?AVString@System@@H@Z)  -- not read this wave
//
// VTABLE: `??_7SpriteSheet@UI@@6B@` IS EXPORTED (retail 0x180154590, per
// PLAN.md §2.2 item D / §4.3 risk 3 -- "147 ??_7 symbols are exported", of
// which SpriteSheet is one of the two the PLAN calls out by name). Since
// ENDGAME B.3.9 UI-D the class wears its compiler table; that export is not
// minted any more.
//
// *** CORRECTION (Wave 7 lane F, 2026-09-07): the SpriteInfo layout below was
// WRONG in two ways in the version this replaces. It said width/height sat at
// +0x18/+0x1C and that +0x20..+0x2C were "UV coords". Both claims came from
// guessing a conventional struct shape rather than reading a write. The real
// offsets, recovered from three independent bodies that all agree (evidence
// table below `SpriteInfo`): width is +0x1C, height is +0x20, the four UVs
// are +0x24/+0x28/+0x2C/+0x30, and there is a previously-unnoticed always-zero
// int32 at +0x18 that no read site in our call surface ever touches. Kept
// here as a comment, per PLAN §6 step 8, so the old (wrong) claim is
// recognisable if it is met again elsewhere (it is not met anywhere else in
// the tree today -- this header was the only place it was written down).
//
// Key behavior:
//   GetSpriteIndexFromName does a LINEAR SCAN over the sprite array comparing
//   System::String contents byte-by-byte (case-sensitive). Returns -1 on failure.
//   Each sprite entry is 0x38 bytes. The name string is at offset 0x00 of each entry.

#include <cstdint>
#include <cstddef>

#include "../object/External.h"
#include "../object/Internal.h"   // ENDGAME B.3.8 RB: InternalMemFree (the concrete slot 0)
#include "../collection/Collection.h" // B4 X7-RT: the sprite array member
#include "../system/String.h"         // B4 X7-RT: the two String members

// The hand-derived row of `Array<SpriteInfo>` (array.cpp, 0x180004e30). Its
// destroy is Destroy38 -> 0x1800059e0 (the two Strings of each 0x38 entry,
// kD_Str2_38), which is what retail's ~Array 0x180004f50 runs before its free.
namespace rtcoll {
extern const ArraySpec kSpec_Sprite38;
}

namespace System   { class Filename; class String; }
namespace Graphics { class MaterialInstance; }

namespace UI {

// ============================================================
// UI::SpriteSheet::SpriteInfo (0x38 bytes per entry)
// Stored in a dynamic array at SpriteSheet::m_sprites (+0x20) -- a
// Collection::Array<SpriteInfo> (src/engine/collection/Collection.h):
// {data, size, capacity}, growth via System::MemoryAllocator under its own
// critical section, all confirmed by FUN_180004e30 (SpriteSheet's array-grow
// body, byte-identical in shape to Collection.h's documented growth policy)
// called from AddSpriteInfo.
//
// EVIDENCE for +0x10 (x), +0x14 (y), +0x18 (reserved), +0x1C (width),
// +0x20 (height): AddSpriteInfo (0x180003390) builds a 0x38-byte temporary on
// its stack before handing it to the array-grow helper. Reading the temporary
// field by field (`local_30`/`iStack_2c` is one CONCAT44 write, `local_28` is
// `(uint)param_5 << 0x20`, `local_20` is `(ulonglong)(uint)param_6`):
//     temp+0x10 = param_3   (low dword of the CONCAT44 -> +0x10, not +0x18)
//     temp+0x14 = param_4   (high dword of the CONCAT44)
//     temp+0x18 = 0         (low dword of the "<<0x20" qword -- ALWAYS ZERO)
//     temp+0x1C = param_5   (high dword of the "<<0x20" qword)
//     temp+0x20 = param_6   (low dword of the plain zero-extend)
//     temp+0x24 = 0         (high dword of the plain zero-extend)
// AddSpriteInfo's mangled name is `(String,String,int,int,int,int)`, so in
// call order that is `AddSpriteInfo(name, group, x=param_3, y=param_4,
// width=param_5, height=param_6)`.
//
// INDEPENDENT CONFIRMATION for width/height, and the discriminator against
// the old +0x18/+0x1C guess: `Sprite::Width` (0x180030bf0) reads
// `entry + 0x1C` and `Sprite::Height` (0x180030c10) reads `entry + 0x20` --
// see Sprite.h. Both are one-line accessors with no other field in play, so
// there is no ambiguity about which field each name belongs to.
//
// EVIDENCE for +0x24/+0x28/+0x2C/+0x30 (u0, v0, u1, v1): `SetSpriteInfo`
// (0x180003460, mangled `(int,int,int,float,float,float,float)`) writes
// param_4..param_7 to exactly those four offsets, and `GetUVs` (0x180003330,
// mangled `(int, float&, float&, float&, float&)`) reads them back in the
// same order into its four out-parameters. SetSpriteInfo also writes
// param_2/param_3 (width/height) to +0x1C/+0x20, confirming those offsets a
// second, independent way.
// ============================================================
// B4 Y RT-UI: the two names are System::String. In the array an entry is only
// built, copied and destroyed by kSpec_Sprite38 / kSpec_Sprite38Z's raw arms
// (init 0x180078950's tail arm, assign 0x180004220, destroy 0x1800059e0:
// +0x08 then +0x00), which name `_data`. The ONE C++ object is
// AddSpriteInfo's local (0x180003390), which retail constructs (the 0x34
// zero bytes), copy-assigns both names into and destroys (+0x08 then +0x00)
// after the append: exactly this struct's implicit member-wise ctor / dtor.
struct SpriteInfo {
    System::String name;        // +0x00  (NULL = empty)
    System::String group;       // +0x08  (NULL = empty)
    int32_t     x;              // +0x10  x position in sprite atlas
    int32_t     y;              // +0x14  y position in sprite atlas
    int32_t     _reserved18;    // +0x18  ALWAYS ZERO from AddSpriteInfo; no read site
                                //        in our call surface ever touches it. Not
                                //        "width" -- that claim was this header's bug.
    int32_t     width;          // +0x1C  CORRECTED (was +0x18). Sprite::Width, AddSpriteInfo, SetSpriteInfo.
    int32_t     height;         // +0x20  CORRECTED (was +0x1C). Sprite::Height, AddSpriteInfo, SetSpriteInfo.
    float       u0;             // +0x24  CORRECTED (was "field20", guessed at +0x20). SetSpriteInfo/GetUVs.
    float       v0;             // +0x28  CORRECTED (was "field24"). SetSpriteInfo/GetUVs.
    float       u1;             // +0x2C  CORRECTED (was "field28"). SetSpriteInfo/GetUVs.
    float       v1;             // +0x30  CORRECTED (was "field2C"). SetSpriteInfo/GetUVs.
    int32_t     _pad34;         // +0x34  CORRECTED (Wave 7 integration, 2026-09-07): NEVER
                                //        WRITTEN, by either implementation. The earlier
                                //        comment here said AddSpriteInfo "zeroes it" -- wrong,
                                //        and the exact same class of mistake this field's own
                                //        offset correction (above) already fixed once. Proven
                                //        by the DISASSEMBLY (not just the decompile) of the
                                //        element copy-construct helper both AddSpriteInfo and
                                //        this port's internal-helper call go through
                                //        (FUN_180004220, retail 0x180004220): its last store is
                                //        `MOV dword ptr [RCX+0x30],EAX` (copying `v1`) then RET
                                //        -- +0x34..+0x37 is never touched, on the source OR the
                                //        destination side, by any caller. A `newEntry._pad34 = 0`
                                //        in the port's C++ has no effect on the shipped entry,
                                //        because the helper never reads or copies that field.
                                //        See kSpriteInfoWrittenBytes below and
                                //        docs/history/runtime_port/WAVE7_LANE_F.md §9.
};

// Bytes a diff oracle may compare between two SpriteInfo entries -- see the
// _pad34 correction above. Comparing the full 0x38 bytes of two entries that
// live in UNRELATED allocations (this port's real array vs. a diff oracle's
// scratch replica) reports a divergence on whatever garbage each allocation
// happened to contain at +0x34, on every single call -- exactly the trap
// reference_oracle_must_not_compare_unwritten_bytes.md describes, and exactly
// what caught this (Wave 7 integration run, 100% divergence on
// UI::SpriteSheet::AddSpriteInfo() until this constant was introduced).
inline constexpr size_t kSpriteInfoWrittenBytes = 0x34;

static_assert(sizeof(SpriteInfo) == 0x38, "SpriteInfo must be 0x38 bytes");
static_assert(offsetof(SpriteInfo, _pad34) == 0x34, "SpriteInfo::_pad34");
static_assert(offsetof(SpriteInfo, name) == 0x00, "name at +0x00");
static_assert(offsetof(SpriteInfo, group) == 0x08, "group at +0x08");
static_assert(offsetof(SpriteInfo, x) == 0x10, "x at +0x10");
static_assert(offsetof(SpriteInfo, y) == 0x14, "y at +0x14");
static_assert(offsetof(SpriteInfo, _reserved18) == 0x18, "_reserved18 at +0x18");
static_assert(offsetof(SpriteInfo, width) == 0x1C, "width at +0x1C");
static_assert(offsetof(SpriteInfo, height) == 0x20, "height at +0x20");
static_assert(offsetof(SpriteInfo, u0) == 0x24, "u0 at +0x24");
static_assert(offsetof(SpriteInfo, v0) == 0x28, "v0 at +0x28");
static_assert(offsetof(SpriteInfo, u1) == 0x2C, "u1 at +0x2C");
static_assert(offsetof(SpriteInfo, v1) == 0x30, "v1 at +0x30");

// ============================================================================
// UI::SpriteSheet — 0x58 bytes, NINE slots, `Object::External`.
// ============================================================================
// ENDGAME B.3.6 block D: the real class under retail's name (RULE B6-4.2 — the
// bare `struct SpriteSheet` this header carried folds in; `::SpriteSheet` and
// `::SpriteInfo` stay as global transition aliases below).
//
// `??_7SpriteSheet@UI@@6B@` @ 0x180154590 (B36_SLOTS.md section 1):
//   [0] +0x00 0x1800037b0  (unexported) vector deleting dtor
//                          (~SpriteSheet 0x18011d670, sizeof 0x58)
//   [1] +0x08 0x180002e50  ?TypeName@SpriteSheet@UI@@UEBAPEBDXZ
//   [2] +0x10 0x180002e30  ?Type@SpriteSheet@UI@@UEBAIXZ
//   [3] +0x18 0x18011d710  ?GetFileVersion@SpriteSheet@UI@@UEBAHXZ  (MOV EAX,6)
//   [4] +0x20 0x18011d720  ?Compile@SpriteSheet@UI@@…  — OVERRIDDEN (RULE B6-1.5)
//   [5] +0x28 0x18011d730  ?Serialize@SpriteSheet@UI@@UEAA_NAEAVStream@IO@@@Z
//   [6] +0x30 0x180003080  ?Reconstruct@SpriteSheet@UI@@UEAAPEAVExternal@Object@@XZ
//   [7] +0x38 0x180003180  ?Destruct@SpriteSheet@UI@@UEAAXXZ
//   [8] +0x40 0x180003100  ?Construct@SpriteSheet@UI@@UEAAPEAVExternal@Object@@XZ — B6-1.1
//
// CONCRETE (ENDGAME B.3.9 UI-D): the minted table, its RT_DELDTOR row and the
// ??0 / ??1 doors are retired. Every construction spells `::new (p)
// UI::SpriteSheet()` -- the exe's thumbnail loaders (B.3.9 EXA), the factory
// pair below, Construct / Reconstruct, and SaveLoad::Draw's one-sprite sheet
// (saveload_draw.cpp).
//
// THE FIELD NAMES ARE THE ONES THE TREE ALREADY SPELLS, and they stay public:
// ~16 unconverted src/engine/ui TUs and saveload_draw.cpp read them through
// the `::SpriteSheet` alias. Three are now NAMED BY EVIDENCE in comments only
// (renaming would touch other blocks' files): +0x30 is the sheet's
// `ExternalPtr<Graphics::MaterialInstance>` (Serialize hands it to that
// instantiation's Serialize, 0x180014520; `GetMaterialInstance` 0x180003320 /
// `SetMaterial` 0x1800034a0 are its accessors), and +0x48/+0x4C/+0x50 are
// `Set`'s two ints and its bool (sprite.cpp, 0x1800034f0).
class SpriteSheet : public Object::External {
public:
    using SpriteInfo = ::UI::SpriteInfo;

    // [0] CONCRETE (ENDGAME B.3.8 block RB, B38_RULES D1/D2): the exe
    // instantiates the class itself (its table copy 0x14015c3b0; ModDialog,
    // FrustumOutlineDescription, the thumb loaders), whose slot 0 is the exe
    // scalar deleting dtor 0x1400121e0 (~SpriteSheet; MemFree on flags & 1).
    // The class keeps that SCALAR arm (B.3.9: an exe-built class); the DLL's
    // vector 0x1800037b0 and its RT_DELDTOR row are retired.
    // `??1SpriteSheet@UI@@UEAA@XZ` 0x18011d670. VIRTUAL (B3X W1); its body is in
    // spritesheet_new.cpp (the two String releases, then ~Array<SpriteInfo>);
    // ~External is the chain's.
    ~SpriteSheet() override;

    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180002e50, Type 0x180002e30, TypeId 0x180002e40, CreateNewObject 0x180002f40
    //         CreateInPlaceObject 0x180003000
    DECLARE_TYPE(SpriteSheet)
public:
    int32_t     GetFileVersion() const override;        // [3] 0x18011d710
    CompileResult Compile(const System::Filename& path) override;  // [4] 0x18011d720 — OVERRIDE
    bool        Serialize(IO::Stream& stream) override;       // [5] 0x18011d730
    Object::External* Reconstruct() override;           // [6] 0x180003080
    void        Destruct() override;                    // [7] 0x180003180
    virtual Object::External* Construct();              // [8] 0x180003100 — B6-1.1

    // ---- ported non-virtual members (src/engine/ui/sprite.cpp) ------------
    int32_t GetSpriteCount() const;                                   // 0x180003190
    int32_t GetSpriteIndexFromName(const System::String& name) const; // 0x1800031f0
    // RETSLOT in retail; a plain by-value return here (not a vtable slot).
    System::String GetSpriteNameFromIndex(int32_t index) const;       // 0x1800032d0
    void GetUVs(int32_t index, float& u0, float& v0, float& u1, float& v1) const;  // 0x180003330
    void AddSpriteInfo(const System::String& name, const System::String& group,
                       int32_t x, int32_t y, int32_t width, int32_t height); // 0x180003390
    void SetSpriteInfo(int32_t index, int32_t width, int32_t height,
                       float u0, float v0, float u1, float v1);        // 0x180003460
    void Set(int32_t p1, int32_t p2, const System::String& p3,
             const System::String& p4, bool p5);                       // 0x1800034f0

    // ---- exported, NOT PORTED, no caller: declared, deliberately undefined --
    int32_t Width(int32_t index) const;                     // 0x1800031a0
    int32_t Height(int32_t index) const;                    // 0x1800031c0
    bool    IsValid(int32_t index) const;                   // 0x1800031e0
    // RETSLOT in retail; by value here.
    Object::ExternalPtr<Graphics::MaterialInstance> GetMaterialInstance() const;  // 0x180003320
    void    SetMaterial(Object::ExternalPtr<Graphics::MaterialInstance> material); // 0x1800034a0
    SpriteSheet(const SpriteSheet& src);                    // 0x1800035d0
    SpriteSheet& operator=(const SpriteSheet& src);         // 0x1800036b0
    // `??2`/`??3` keep their DOORS in spritesheet_new.cpp (B36_RULES
    // section 7). The factory pair is the class's own (ENDGAME B.3.9 UI-D,
    // spritesheet_vtable.cpp), registered by name and address.
    static void* operator new(std::size_t bytes);              // 0x180002e60  (door)
    static void* operator new(std::size_t bytes, void* place); // 0x180002ed0
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                     // 0x180002ee0  (door)

    // `??0SpriteSheet@UI@@QEAA@XZ` 0x18011d600 — the REAL constructor, PUBLIC:
    // External() and the stamps are the compiler's, then the stores below;
    // +0x51..+0x57 are never written.
    // B4 X7-RT: the Array and the two Strings are members; m_materialInstance joins
    // them in the init list so the stores keep retail's order (+0x20/+0x28,
    // +0x30, +0x38, +0x40, then the body's +0x48 / +0x50).
    SpriteSheet()
        : m_sprites(), m_materialInstance(nullptr), m_textureName(), m_groupName() {
        m_imageWidth = 0;                        // +0x48 } one qword
        m_imageHeight = 0;                        //       }
        m_padForFiltering = false;                    // +0x50 (BYTE)
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md, DESIGN.md hook rule 5):
    // ext.create_on_demand's sprite-sheet prefetch. Null by default and under the faithful profile.
    // Run at the end of a READING Serialize, with the sheet holding its material: the extension queues
    // the first-use creates of its textures, so a window that opens draws sheets already made.
    static void (*_extLoaded)(const SpriteSheet& sheet);

public:
    // ---- the 0x38 bytes SpriteSheet adds after Object::External ------------
    // B4 X7-RT (`behaviour:`): typed members. Retail's ~SpriteSheet
    // 0x18011d670 releases +0x40, then +0x38, then calls ~Array<SpriteInfo>
    // 0x180004f50 on +0x20 (each entry's two Strings, then the buffer), then
    // ~External: reverse declaration order after the (empty) body.
    Collection::Array<SpriteInfo, Collection::DefaultAllocator,
                      Collection::ExplicitSpec<&rtcoll::kSpec_Sprite38>>
                m_sprites;          // +0x20  {data, size +0x28 (GetSpriteCount), capacity +0x2c}
    Graphics::MaterialInstance* m_materialInstance; // +0x30  ExternalPtr<Graphics::MaterialInstance> (banner; B4 P2 H4: typed, serializer 0x180014520)
    System::String m_textureName;   // +0x38  Set's p3
    System::String m_groupName;     // +0x40  Set's p4
    int32_t     m_imageWidth;          // +0x48  Set's p1
    int32_t     m_imageHeight;          // +0x4C  Set's p2
    bool        m_padForFiltering;          // +0x50  Set's p5; Serialize(bool&) 0x18011d7ba
    uint8_t     _pad51[7];          // +0x51  NEVER WRITTEN by the ctor (its last
                                    //        store is `MOV byte ptr [RCX+0x50],DL`,
                                    //        0x18011d65d) — kSpriteSheetWrittenBytes
};

static_assert(sizeof(SpriteSheet) == 0x58,
              "UI::SpriteSheet is 0x58 bytes (MOV EDX,0x58 in slot 0, 0x1800037b0)");
static_assert(offsetof(SpriteSheet, _pad51) == 0x51, "SpriteSheet::_pad51");
static_assert(alignof(SpriteSheet) == 8, "UI::SpriteSheet aligns to 8 (the vptr)");
static_assert(offsetof(SpriteSheet, m_sprites) == 0x20, "m_sprites at +0x20");
static_assert(offsetof(SpriteSheet, m_sprites) + 0x08 == 0x28, "the sprite count at +0x28");
static_assert(offsetof(SpriteSheet, m_sprites) + 0x0c == 0x2C, "the sprite capacity at +0x2C");
static_assert(offsetof(SpriteSheet, m_materialInstance) == 0x30, "m_field30 at +0x30");
static_assert(offsetof(SpriteSheet, m_textureName) == 0x38, "m_textureName at +0x38");
static_assert(offsetof(SpriteSheet, m_groupName) == 0x40, "m_groupName at +0x40");
static_assert(offsetof(SpriteSheet, m_imageWidth) == 0x48, "m_field48 at +0x48");
static_assert(offsetof(SpriteSheet, m_imageHeight) == 0x4C, "m_field4C at +0x4C");
static_assert(offsetof(SpriteSheet, m_padForFiltering) == 0x50, "m_field50 at +0x50");
static_assert(offsetof(SpriteSheet, m_sprites) == sizeof(Object::External),
              "SpriteSheet's own bytes begin right after Object::External");

}  // namespace UI

// Bytes a diff oracle may compare between two freshly-constructed SpriteSheet
// objects. The ctor writes through and including +0x50 (0x51 bytes total) and
// never touches +0x51..+0x57 -- comparing the full 0x58-byte sizeof() reports
// a divergence on whatever the allocator happened to leave there, which is
// unrelated between a real object and a diff oracle's scratch buffer.
inline constexpr size_t kSpriteSheetWrittenBytes = 0x51;

// RULE B6-4.2 transition aliases: the bare names ~16 src/engine/ui TUs,
// saveload_draw.cpp and src/engine/common.h spell.
using SpriteSheet = UI::SpriteSheet;
using SpriteInfo  = UI::SpriteInfo;

// ==== B3R W0-P0c: Runtime bodies reached by retail address ====
// ENDGAME B.3R (B3R_RULES R5): each body below is registered at the retail
// address in its trailing comment and read through the registry today. It is
// declared here, copied from its definition (linkage, namespace, types), so the
// reader can call it by name. Nothing calls it through this declaration yet.
namespace IO { class Serializer; }   // io/Serializer.h

// ---- ui/spritesheet_vtable.cpp
extern "C" void RtUi_SpriteSheet_SerializeEntries(IO::Serializer* ser, int32_t count, UI::SpriteInfo* entries);  // 0x180078ad0
// ==== end B3R W0-P0c ====
