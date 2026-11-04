#pragma once
// ============================================================================
// src/engine/ui/StringTable.h — UI::StringTable, 0x30 bytes, 9 slots
// ============================================================================
//
// ENDGAME B.3.6 block D (RULE B6-4.2 / B6-8): the real class under retail's
// name. This header used to carry a bare `UiStringTable` POD, a bare
// `StringTableTextEntry` and a `StringTableOffsets` sub-namespace; all three
// fold in here, and `UiStringTable` / `StringTableTextEntry` stay as global
// transition aliases.
//
// THE LAYOUT, and where each fact was read (Runtime-x64.dll, image base
// 0x180000000):
//   sizeof 0x30   `?CreateNewObject@StringTable@UI@@` (0x1800022b1):
//                 `MOV EDX,0x30 ; LEA R8D,[RDX-0x28]` -> Allocate(0x30, 8);
//                 corroborated by B36_SLOTS.md section 1 (the slot-0 body's
//                 `MOV EDX,0x30`).
//   +0x20..+0x2f  a Collection::Array<TextEntry> {data, size, capacity}:
//                 `GetCount` (0x180002610) is `return *(int*)(this+0x28)`, and
//                 GetText(int) (0x180002550) / GetIndexFromName (0x180002470)
//                 walk a stride-0x10 array at `*(this+0x20)` bounded by it. The
//                 ctor `??0StringTable@UI@@QEAA@XZ` (0x18011d890) zeroes +0x20
//                 and +0x28 (one 8-byte store covering size AND capacity).
//   TextEntry     stride 0x10: +0x00 the NAME (GetIndexFromName compares it),
//                 +0x08 the TEXT (GetText bumps and returns it), both
//                 refcounted System::String data pointers.
//   parent        "Object::External" (`?_ParentTypeName@StringTable@UI@@`
//                 0x180002160).
//
// `??_7StringTable@UI@@6B@` @ 0x180154630, NINE slots (B36_SLOTS.md):
//   [0] +0x00 0x180002740  (unexported) vector deleting dtor — the class's own
//                          (B.3.9); it had no RT_DELDTOR row, its body was the
//                          by-address `RtUiStringTable_ScalarDelete` (retired)
//   [1] +0x08 0x180002190  ?TypeName@StringTable@UI@@UEBAPEBDXZ
//   [2] +0x10 0x180002170  ?Type@StringTable@UI@@UEBAIXZ
//   [3] +0x18 0x18011d930  ?GetFileVersion@StringTable@UI@@UEBAHXZ  (MOV EAX,1)
//   [4] +0x20 0x1800066e0  ?Compile@External@Object@@  INHERITED — NOT declared
//   [5] +0x28 0x18011d940  ?Serialize@StringTable@UI@@UEAA_NAEAVStream@IO@@@Z
//   [6] +0x30 0x180002390  ?Reconstruct@StringTable@UI@@UEAAPEAVExternal@Object@@XZ
//   [7] +0x38 0x180002460  ?Destruct@StringTable@UI@@UEAAXXZ
//   [8] +0x40 0x180002400  ?Construct@StringTable@UI@@UEAAPEAVExternal@Object@@XZ — B6-1.1
//
// ENDGAME B.3 step 9 (block UI-D, B39_RULES D1/D2): CONCRETE, the
// CharacterSet.h shape. The real public ctor is retail's `??0StringTable`
// (0x18011d890) minus its vptr stores; slot 0 the class's own vector deleting
// dtor; ~StringTable virtual, with the retired ??1 door's
// body. The factory pair is the class's own (stringtable_factory.cpp); the
// minted `??_7StringTable@UI@@6B@` is retired.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../object/External.h"
#include "../object/Internal.h"   // Object::detail::InternalMemFree
#include "../collection/Collection.h"   // B4 Y RT-UI: the entry array member
#include "../system/String.h"           // TextEntry's two Strings

// B4 Y RT-UI: the hand-derived row StringTable's entry array is built and
// destroyed through (src/engine/collection/array.cpp, retail SetSize
// 0x180078c90), named here so the typed member can carry it as its spec.
namespace rtcoll {
extern const ArraySpec kSpec_StringPair10;   // 0x180078c90  Array<StringTable::TextEntry>
}  // namespace rtcoll

namespace UI {

class StringTable : public Object::External {
public:
    // One row of the table, stride 0x10 (see the banner). Retail's nested
    // record; its name is not exported, so it is named for what it holds.
    struct TextEntry {
        // B4 Y RT-UI: typed. An entry is only ever built, copied and destroyed
        // by kSpec_StringPair10's arms (assign 0x180078d33 / 0x180078d44, destroy
        // 0x180005570), which store `_data` raw: no C++ entry object is
        // constructed or destroyed, so each String is released once.
        System::String name;   // +0x00  the key (NULL = empty)
        System::String text;   // +0x08  the value (NULL = empty)
    };

    // ??0StringTable@UI@@QEAA@XZ 0x18011d890 — the REAL constructor: External()
    // (+0x08..+0x18), then +0x20 = 0 and ONE 8-byte store covering _size and
    // _capacity (18011d8d5 / 18011d8d9): `_entries`' own Array() (B4 Y RT-UI).
    StringTable() : Object::External() {}

    // [0] 0x180002740 — MSVC's VECTOR deleting destructor (`MOV EDX,0x30`, the
    // element size; the scalar arm is ??1 inlined, then ??3StringTable — one
    // body with the global MemFree, stringtable_lifecycle.cpp's banner).
    // 0x18011d8f0 — an empty body: `_entries`' ~Array (0x180004a90), then
    // ~External, both compiler-chained (stringtable_factory.cpp).
    ~StringTable() override;

    // ENDGAME B.3.9 TI-UI: ONE reflection block (B39 D6); `_typeInfo` is the
    // class's own record (DEFINE_TYPE in the class's TU), no longer minted.
    // retail: TypeName 0x180002190, Type 0x180002170, CreateNewObject 0x180002280, CreateInPlaceObject 0x180002330
    DECLARE_TYPE(StringTable)
public:
    int32_t     GetFileVersion() const override;        // [3] 0x18011d930
    // [4] 0x1800066e0 is Object::External::Compile, inherited unchanged.
    bool        Serialize(IO::Stream& stream) override;       // [5] 0x18011d940
    Object::External* Reconstruct() override;           // [6] 0x180002390
    void        Destruct() override;                    // [7] 0x180002460
    virtual Object::External* Construct();              // [8] 0x180002400 — B6-1.1

    // ---- ported non-virtual members (src/engine/ui/sprite.cpp) ------------
    int32_t GetCount() const;                                       // 0x180002610
    // The two GetText overloads — non-virtual, THE DOOR DECIDES THE OVERLOAD.
    // Both are RETSLOT in retail; plain by-value returns here.
    System::String GetText(int32_t index) const;                    // 0x180002550
    // NOT RETAIL (review F-009): the body of GetText(int), static so that
    // UI::String{null, -1} (operator System::String) reaches it without a member
    // call on a null table, which C++ leaves undefined. Retail's guard order.
    static System::String GetTextOf(const StringTable* table, int32_t index);
    System::String GetText(const System::String& name) const;       // 0x1800025b0
    int32_t GetIndexFromName(const System::String& name) const;     // 0x180002470

    // ---- exported, NOT PORTED, no caller: declared, deliberately undefined --
    StringTable(const StringTable& src);                // 0x180002620  not ported
    StringTable& operator=(const StringTable& src);     // 0x1800026b0  not ported
    // 0x180002180 — the minted record's type id (stringtable_factory.cpp).
    // Placement is spelt `::new (p) StringTable()` (B9-5): these stay undefined;
    // `??3` is the inline member below (its C door and stringtable_lifecycle.cpp
    // went in B.4 R0: unreferenced).
    static void* operator new(std::size_t bytes);              // 0x1800021a0
    static void* operator new(std::size_t bytes, void* place); // 0x180002210
    static void  operator delete(void* p) { ::Object::detail::InternalMemFree(p); }                     // 0x180002220
    // Retail's factory pair (B39_RULES D2), defined and registered in
    // stringtable_factory.cpp.

public:
    // ---- the 0x10 bytes StringTable adds: Collection::Array<TextEntry> -----
    // B4 Y RT-UI: a real member on retail's own row, `kSpec_StringPair10`
    // (0x180078c90, the array's SetSize). Its ~Array is the row's destroy
    // (0x180005570: per entry +0x08 then +0x00) over the live range, then the
    // J1 free and the header zeroed: retail's 0x180004a90, which ~StringTable
    // 0x18011d8f0 calls before the tail JMP to ~External.
    using EntryArray = Collection::Array<TextEntry, Collection::DefaultAllocator,
                                         Collection::ExplicitSpec<&rtcoll::kSpec_StringPair10>>;
    EntryArray _entries;    // +0x20  data, +0x28 size (GetCount), +0x2C capacity
};

static_assert(sizeof(StringTable::TextEntry) == 0x10, "StringTable entry stride 0x10");
static_assert(offsetof(StringTable::TextEntry, name) == 0x00, "entry name at +0x00");
static_assert(offsetof(StringTable::TextEntry, text) == 0x08, "entry text at +0x08");
static_assert(sizeof(StringTable) == 0x30,
              "UI::StringTable is 0x30 bytes (CreateNewObject 0x1800022b1 allocates 0x30)");
static_assert(alignof(StringTable) == 8, "UI::StringTable aligns to 8 (the vptr)");
static_assert(offsetof(StringTable, _entries) == 0x20, "GetText 0x180002550 reads *(this+0x20)");
static_assert(offsetof(StringTable, _entries) + 0x08 == 0x28, "GetCount 0x180002610 reads +0x28");
static_assert(offsetof(StringTable, _entries) + 0x0c == 0x2C, "Array capacity at +0x2C");
static_assert(offsetof(StringTable, _entries) == sizeof(Object::External),
              "StringTable's own bytes begin right after Object::External");

}  // namespace UI

// RULE B6-4.2 transition aliases for the three src/engine/ui TUs that spell
// the old bare names.
using UiStringTable        = UI::StringTable;
using StringTableTextEntry = UI::StringTable::TextEntry;
using StringTable          = UI::StringTable;
