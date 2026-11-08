// ============================================================================
// tests/unit/ui_resources_abi_test.cpp — ENDGAME B.3.6 block D
//   the seven UI `Object::External` resources as real C++ classes
//
//   UI::CharacterSet  0x28   UI::Cursor       0x30   UI::StringTable  0x30
//   UI::SpriteSheet   0x58   UI::SpriteCapture 0x58  UI::Font         0x90
//   (UI::Dialog, the seventh 9-slot External in B36_SLOTS, is block B's)
//
// All abstract, all 9 slots, all directly on Object::External, `Construct`
// introduced at slot 8 (RULE B6-1.1), and ONLY `Font` and `SpriteSheet`
// override slot 4 `Compile` (RULE B6-1.5) — the other four carry
// `?Compile@External@Object@@` (0x1800066e0) unchanged.
//
// Built to the contract of graphics_material_abi_test.cpp (block G, step 5):
//   PART A — LAYOUT: size, alignment, every field offset the headers pin, and
//            that each class's own bytes begin at sizeof(Object::External).
//   PART B — SLOT ORDER: a probe subclass per class, its GCC vtable read back by
//            BYTE OFFSET and raw-called; each marker body records which slot
//            ran. Nine distinct entries, `Construct` at +0x40.
//   PART C — SLOT 4: shared with a bare External leaf on four classes,
//            overridden (a different pointer that answers retail's 1) on two.
//   PART D — THE QUALIFIED CALL: a door that delegates with
//            `reinterpret_cast<UI::C*>(self)->UI::C::M()` does not re-enter
//            itself through a minted table whose slot points back at the door.
//
// The real member bodies live in src/engine/ui/*.cpp and stand on the port
// registry, so they are not linked here: the out-of-line members are defined as
// MARKERS at the bottom of this file (one definition per binary — none of those
// .cpp files is in UNIT_TREE_SRCS), which also makes GCC emit the vtables here.
// ============================================================================
#include "unit.h"
#include "slot_check.h"
#include "dtor_check.h"

#include "ui/CharacterSet.h"
#include "ui/Cursor.h"
#include "ui/StringTable.h"
#include "ui/SpriteSheet.h"
#include "ui/SpriteCapture.h"
#include "ui/Font.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <new>

namespace {

const void* g_lastSelf = nullptr;
int         g_lastMark = -1;
void Note(const void* self, int mark) { g_lastSelf = self; g_lastMark = mark; }
void Reset() { g_lastSelf = nullptr; g_lastMark = -1; }

// ENDGAME B.3.9 UI-D: the destructor markers of the CONCRETE classes RECORD
// each call, so the destructors (B3X W1: D1, D0 and delete[], the retired
// slot-0 arms) can be seen running ~X once per element and in retail's
// DESCENDING order.
int         g_b39DtorCount = 0;
const void* g_b39DtorOrder[8] = {};
void B39NoteDtor(const void* self) {
    if (g_b39DtorCount < 8) g_b39DtorOrder[g_b39DtorCount] = self;
    ++g_b39DtorCount;
}

// The offset / index is RETAIL's; T decides the compiler's. Default T: every object this file reads raw is a UI class, all Object::Base-rooted
// (a virtual destructor is inherited, so the root answers for every class).
template <typename Fn, class T = Object::Base>
Fn SlotAt(void* obj, int byteOffset) {
    const uint8_t* vt = *reinterpret_cast<const uint8_t* const*>(obj);
    Fn f = nullptr;
    const void* raw = *reinterpret_cast<const void* const*>(vt + RetailByteOffset<T>(byteOffset));
    std::memcpy(&f, &raw, sizeof f);
    return f;
}
const void* const* VTableOf(void* obj) {
    return *reinterpret_cast<const void* const* const*>(obj);
}

constexpr int kRetailDtor     = 0x00;   // B3X W1: the destructor pair (D1, D0)
constexpr int kTypeName       = 0x08;
constexpr int kType           = 0x10;
constexpr int kGetFileVersion = 0x18;
constexpr int kCompile        = 0x20;
constexpr int kSerialize      = 0x28;
constexpr int kReconstruct    = 0x30;
constexpr int kDestruct       = 0x38;
constexpr int kConstruct      = 0x40;   // introduced per leaf — RULE B6-1.1

// A bare Object::External leaf: whatever it carries at +0x20 IS
// `?Compile@External@Object@@`.
struct BareExternalProbe final : Object::External {
    int32_t GetFileVersion() const override { return 0; }
    bool Serialize(IO::Stream&) override { return true; }
    Object::External* Reconstruct() override { return this; }
    void Destruct() override {}
};

// B3X W1: retail slot 0 is the destructor pair; each probe's destructor
// records mark 0 and a recording class-scope operator delete stands for
// retail flags 1's MemFree.
struct CharacterSetProbe final : UI::CharacterSet {
    ~CharacterSetProbe() override { Note(this, 0); }
    DTOR_CHECK_RECORDING_DELETE
};

// Retail slot 0 read raw: compiler [0] D1 runs ~Probe once and frees nothing
// (flags 0), on a placement-new'd probe that is never destroyed again (a
// stack probe would be destroyed twice); [1] D0 runs it and hands the block
// to the recording operator delete (flags 1).
template <typename Probe>
void CheckProbeDtorPair() {
    alignas(Probe) unsigned char buf[sizeof(Probe)];
    Probe* p = ::new (static_cast<void*>(buf)) Probe();
    Reset(); dtor_check::ResetFrees();
    dtor_check::CallD1(p);
    CHECK_EQ(g_lastMark, kRetailDtor / 8); CHECK(g_lastSelf == p);
    CHECK_EQ(dtor_check::g_frees, 0);
    Probe* h = dtor_check::NewOnMalloc<Probe>();
    Reset(); dtor_check::ResetFrees();
    dtor_check::CallD0(h);
    CHECK_EQ(g_lastMark, kRetailDtor / 8); CHECK(g_lastSelf == h);
    CHECK_EQ(dtor_check::g_frees, 1); CHECK(dtor_check::g_lastFree == h);
}

// Raw-call slots 0,1,2,3,5,6,7,8 and check the marker each one leaves (slot 0
// through CheckProbeDtorPair on a fresh Probe).
template <typename Probe>
void CheckNineSlots(void* raw, const ::Object::BaseTypeInfo& rec) {
    using NameFn = const char* (*)(const void*);
    using TypeFn = uint32_t (*)(const void*);
    using VerFn  = int32_t (*)(const void*);
    using SerFn  = bool (*)(void*, void*);
    using PtrFn  = void* (*)(void*);
    using VoidFn = void (*)(void*);

    CheckProbeDtorPair<Probe>();
    // Slots 1/2 (ENDGAME B.3.9 TI-UI): DECLARE_TYPE's bodies over the own record.
    CHECK(SlotAt<NameFn>(raw, kTypeName)(raw) == rec.name);
    CHECK_EQ(SlotAt<TypeFn>(raw, kType)(raw), rec.typeId);
    Reset(); SlotAt<VerFn>(raw, kGetFileVersion)(raw);  CHECK_EQ(g_lastMark, kGetFileVersion / 8);
    Reset(); SlotAt<SerFn>(raw, kSerialize)(raw, nullptr); CHECK_EQ(g_lastMark, kSerialize / 8);
    Reset(); SlotAt<PtrFn>(raw, kReconstruct)(raw);     CHECK_EQ(g_lastMark, kReconstruct / 8);
    Reset(); SlotAt<VoidFn>(raw, kDestruct)(raw);       CHECK_EQ(g_lastMark, kDestruct / 8);
    Reset(); SlotAt<PtrFn>(raw, kConstruct)(raw);       CHECK_EQ(g_lastMark, kConstruct / 8);
    CHECK(g_lastSelf == raw);

    // Nine retail entries = ten compiler entries, all distinct (D1 != D0).
    const void* const* vt = VTableOf(raw);
    for (int i = 0; i < 10; ++i)
        for (int j = i + 1; j < 10; ++j)
            CHECK(vt[i] != vt[j]);
}

// Slot 4 is Object::External's own body (inherited) on four of the six.
void CheckCompileInherited(void* raw) {
    BareExternalProbe bare;
    CHECK(RetailEntry<Object::Base>(raw, kCompile / 8) == RetailEntry<Object::Base>(static_cast<void*>(&bare), kCompile / 8));
}

// ---------------------------------------------------------------------------
// UI::CharacterSet
// ---------------------------------------------------------------------------
UNIT_TEST(ui_characterset_layout) {
    CHECK_EQ((int)alignof(UI::CharacterSet), 8);
    CHECK_EQ((int)offsetof(UI::CharacterSet, _characters), (int)sizeof(Object::External));
    CHECK((std::is_base_of<Object::External, UI::CharacterSet>::value));
    CHECK(!std::is_abstract<UI::CharacterSet>::value);      // B.3.9 UI-D: concrete
    CHECK(std::is_destructible<UI::CharacterSet>::value);   // B3X W1: public virtual dtor
    CHECK(std::has_virtual_destructor<UI::CharacterSet>::value);
    CHECK(std::is_default_constructible<UI::CharacterSet>::value);   // B3X W1: public ctor + public dtor
}

// ---------------------------------------------------------------------------
// UI::Cursor
// ---------------------------------------------------------------------------
struct CursorProbe final : UI::Cursor {
    ~CursorProbe() override { Note(this, 0); }
    DTOR_CHECK_RECORDING_DELETE
};

UNIT_TEST(ui_cursor_layout) {
    CHECK_EQ((int)alignof(UI::Cursor), 8);
    CHECK_EQ((int)offsetof(UI::Cursor, _hotSpotX), (int)sizeof(Object::External));
    CHECK((std::is_base_of<Object::External, UI::Cursor>::value));
    CHECK(!std::is_abstract<UI::Cursor>::value);       // B.3.9 UI-D: concrete
    CHECK(std::is_destructible<UI::Cursor>::value);    // B3X W1: public virtual dtor
    CHECK(std::has_virtual_destructor<UI::Cursor>::value);

    // The three getters read exactly the bytes retail's do (0x18003b250/260/270).
    CursorProbe probe;
    uint8_t* bytes = reinterpret_cast<uint8_t*>(static_cast<UI::Cursor*>(&probe));
    *reinterpret_cast<int32_t*>(bytes + 0x20) = 7;
    *reinterpret_cast<int32_t*>(bytes + 0x24) = -3;
    *reinterpret_cast<void**>(bytes + 0x28) = bytes;
    CHECK_EQ(probe.GetHotSpotX(), 7);
    CHECK_EQ(probe.GetHotSpotY(), -3);
    CHECK(static_cast<void*>(probe.GetSpriteSheet().Get()) == static_cast<void*>(bytes));
}

UNIT_TEST(ui_cursor_slot_order_is_retails) {
    CursorProbe probe;
    void* raw = static_cast<void*>(&probe);
    CheckNineSlots<CursorProbe>(raw, UI::Cursor::GetTypeInfo());
    CheckCompileInherited(raw);
}

// ---------------------------------------------------------------------------
// UI::StringTable
// ---------------------------------------------------------------------------
struct StringTableProbe final : UI::StringTable {
    ~StringTableProbe() override { Note(this, 0); }
    DTOR_CHECK_RECORDING_DELETE
};

UNIT_TEST(ui_stringtable_layout) {
    CHECK_EQ((int)alignof(UI::StringTable), 8);
    CHECK_EQ((int)(offsetof(UI::StringTable, _entries) + 0x08),  0x28);   // _size
    CHECK_EQ((int)(offsetof(UI::StringTable, _entries) + 0x0c),  0x2c);   // _capacity
    CHECK_EQ((int)offsetof(UI::StringTable, _entries), (int)sizeof(Object::External));
    CHECK((std::is_base_of<Object::External, UI::StringTable>::value));
    CHECK(!std::is_abstract<UI::StringTable>::value);       // B.3.9 UI-D: concrete
    CHECK(std::is_destructible<UI::StringTable>::value);    // B3X W1: public virtual dtor
    CHECK(std::has_virtual_destructor<UI::StringTable>::value);
    // The B6-4.2 transition aliases name the class and its row.
    CHECK((std::is_same<::UiStringTable, UI::StringTable>::value));
    CHECK((std::is_same<::StringTable, UI::StringTable>::value));
    CHECK((std::is_same<::StringTableTextEntry, UI::StringTable::TextEntry>::value));
}

UNIT_TEST(ui_stringtable_slot_order_is_retails) {
    StringTableProbe probe;
    void* raw = static_cast<void*>(&probe);
    CheckNineSlots<StringTableProbe>(raw, UI::StringTable::GetTypeInfo());
    CheckCompileInherited(raw);
}

// Slot 4 is the class's OWN body on Font and SpriteSheet (RULE B6-1.5): a
// different pointer from Object::External's, that runs the class's marker and
// answers retail's 1. `?AW4CompileResult` is a 4-byte enum, returned in EAX
// with no hidden slot, so the raw call is safe.
void CheckCompileOverridden(void* raw) {
    BareExternalProbe bare;
    CHECK(RetailEntry<Object::Base>(raw, kCompile / 8) != RetailEntry<Object::Base>(static_cast<void*>(&bare), kCompile / 8));
    using CompileFn = uint32_t (*)(void*, const void*);
    Reset();
    const uint32_t r = SlotAt<CompileFn>(raw, kCompile)(raw, nullptr);
    CHECK_EQ(g_lastMark, kCompile / 8);
    CHECK_EQ(r, 1u);
}

// ---------------------------------------------------------------------------
// UI::SpriteSheet
// ---------------------------------------------------------------------------
struct SpriteSheetProbe final : UI::SpriteSheet {
    ~SpriteSheetProbe() override { Note(this, 0); }
    DTOR_CHECK_RECORDING_DELETE
};

UNIT_TEST(ui_spritesheet_layout) {
    CHECK_EQ((int)alignof(UI::SpriteSheet), 8);
    CHECK_EQ((int)(offsetof(UI::SpriteSheet, m_sprites) + 8), 0x28);   // the sprite count
    CHECK_EQ((int)offsetof(UI::SpriteSheet, m_sprites), (int)sizeof(Object::External));
    CHECK((std::is_base_of<Object::External, UI::SpriteSheet>::value));
    CHECK(!std::is_abstract<UI::SpriteSheet>::value);    // concrete since B.3.8 RB (exe table copy 0x14015c3b0)
    CHECK((std::is_same<::SpriteSheet, UI::SpriteSheet>::value));
    CHECK((std::is_same<::SpriteInfo, UI::SpriteInfo>::value));
    CHECK((std::is_same<UI::SpriteSheet::SpriteInfo, UI::SpriteInfo>::value));
}

UNIT_TEST(ui_spritesheet_slot_order_is_retails) {
    SpriteSheetProbe probe;
    void* raw = static_cast<void*>(&probe);
    CheckNineSlots<SpriteSheetProbe>(raw, UI::SpriteSheet::GetTypeInfo());
    CheckCompileOverridden(raw);
}

// ---------------------------------------------------------------------------
// UI::SpriteCapture
// ---------------------------------------------------------------------------
struct SpriteCaptureProbe final : UI::SpriteCapture {
    ~SpriteCaptureProbe() override { Note(this, 0); }
    DTOR_CHECK_RECORDING_DELETE
};

UNIT_TEST(ui_spritecapture_layout) {
    CHECK_EQ((int)alignof(UI::SpriteCapture), 8);
    CHECK_EQ((int)offsetof(UI::SpriteCapture, _sheets), (int)sizeof(Object::External));
    CHECK((std::is_base_of<Object::External, UI::SpriteCapture>::value));
    CHECK(!std::is_abstract<UI::SpriteCapture>::value);  // concrete since B.3.8 RB (exe table copy 0x14015d550)

    SpriteCaptureProbe probe;
    probe._width = 12;
    probe._height = 34;
    CHECK_EQ(probe.GetWidth(), 12);      // 0x1800d9220 MOV EAX,[RCX+0x50]
    CHECK_EQ(probe.GetHeight(), 34);     // 0x1800d9210 MOV EAX,[RCX+0x54]
}

UNIT_TEST(ui_spritecapture_slot_order_is_retails) {
    SpriteCaptureProbe probe;
    void* raw = static_cast<void*>(&probe);
    CheckNineSlots<SpriteCaptureProbe>(raw, UI::SpriteCapture::GetTypeInfo());
    CheckCompileInherited(raw);
}

// ---------------------------------------------------------------------------
// UI::Font
// ---------------------------------------------------------------------------
struct FontProbe final : UI::Font {
    ~FontProbe() override { Note(this, 0); }
    DTOR_CHECK_RECORDING_DELETE
};

UNIT_TEST(ui_font_layout) {
    CHECK_EQ((int)alignof(UI::Font), 8);
    CHECK_EQ((int)offsetof(UI::Font, _fontName), (int)sizeof(Object::External));
    CHECK_EQ((int)sizeof(UI::Font::Kerning), 0x10);
    CHECK((std::is_base_of<Object::External, UI::Font>::value));
    CHECK(!std::is_abstract<UI::Font>::value);       // B.3.9 UI-D: concrete
    CHECK(std::is_destructible<UI::Font>::value);    // B3X W1: public virtual dtor
    CHECK(std::has_virtual_destructor<UI::Font>::value);
}

// The getters read exactly the bytes retail's do (read this pass).
UNIT_TEST(ui_font_getters_read_retails_offsets) {
    FontProbe probe;
    int32_t table[3] = { 5, 0xffff, 7 };
    UI::Font::Kerning rows[2] = { {1, 2, 3, 4}, {5, 6, 7, 8} };
    probe._charToGlyph._data = table;  probe._charToGlyph._size = 3;
    probe._glyphs._data = rows;        probe._glyphs._size = 2;
    probe._lineHeight = 11; probe._baseLine = 9; probe._spaceWidth = 4;

    CHECK_EQ(probe.GetHeight(), 11);          // 0x180031de0  +0x50
    CHECK_EQ(probe.GetBaseLine(), 9);         // 0x180031df0  +0x54
    CHECK_EQ(probe.GetSpaceWidth(), 4);       // 0x180031e00  +0x5c
    CHECK(&probe.GetKern(1) == &rows[1]);     // 0x180031dd0  &_glyphs[i]
    CHECK_EQ(probe.GetSpriteIndex(0), 5);     // 0x180031da0
    CHECK_EQ(probe.GetSpriteIndex(1), -1);    //   0xffff -> -1
    CHECK_EQ(probe.GetSpriteIndex(3), -1);    //   out of range -> -1
    CHECK_EQ(probe.GetSpriteIndex(-1), -1);

    // Detach the stack arrays before the probe's member ~Array runs.
    probe._charToGlyph._data = nullptr; probe._charToGlyph._size = 0;
    probe._glyphs._data = nullptr;      probe._glyphs._size = 0;
}

UNIT_TEST(ui_font_slot_order_is_retails) {
    FontProbe probe;
    void* raw = static_cast<void*>(&probe);
    CheckNineSlots<FontProbe>(raw, UI::Font::GetTypeInfo());
    CheckCompileOverridden(raw);
}

// RULE B6-1 across the six resources at once: nine slots each, `Construct`
// at +0x40 distinct from slots 0..7; slot 4 inherited on four, overridden on
// Font and SpriteSheet.
UNIT_TEST(ui_block_d_resources_construct_at_slot8) {
    CharacterSetProbe cs; CursorProbe cu; StringTableProbe st;
    SpriteSheetProbe ss; SpriteCaptureProbe sc; FontProbe fo;
    void* all[6] = { &cs, &cu, &st, &ss, &sc, &fo };
    for (int c = 0; c < 6; ++c) {
        Reset();
        using PtrFn = void* (*)(void*);
        SlotAt<PtrFn>(all[c], kConstruct)(all[c]);
        CHECK_EQ(g_lastMark, kConstruct / 8);
        CHECK(g_lastSelf == all[c]);
        // Retail slots 0..7 = compiler slots 0..8 (D1, D0, then 1..7).
        for (int i = 0; i < 9; ++i)
            CHECK(VTableOf(all[c])[i] != RetailEntry<Object::Base>(all[c], kConstruct / 8));
    }
}

// ===========================================================================
// ENDGAME B.3.9 UI-D (B39_RULES D1/D2, rule B9-16) — the CONCRETE classes.
// ===========================================================================

// B3X W1: retail's flags-1 arm is `delete one` (T's own D0: the chain once,
// then T's class-scope operator delete, InternalMemFree == free here); the
// free is PROVEN on a dtor_check::Recording<T> stand-in. Retail's flags-3
// vector arm has no Itanium counterpart: `delete[]` of `new T[3]` runs three
// destructors, downward. `Make` placement-constructs one real object.
template <typename T, typename Make>
void B39ExpectBothArms(size_t elemSize, Make make) {
    static_assert(dtor_check::HasClassDelete<T>::value, "D0 frees through a class-scope operator delete");
    CHECK_EQ(sizeof(T), elemSize);
    void* p = Object::detail::InternalMemAlloc(elemSize, 8);
    CHECK(p != nullptr);
    T* one = make(p);
    g_b39DtorCount = 0;
    delete one;
    CHECK_EQ(g_b39DtorCount, 1);

    T* r = dtor_check::NewOnMalloc<dtor_check::Recording<T>>();
    g_b39DtorCount = 0;
    dtor_check::ResetFrees();
    delete r;
    CHECK_EQ(g_b39DtorCount, 1);
    CHECK_EQ(dtor_check::g_frees, 1);
    CHECK(dtor_check::g_lastFree == r);

    CHECK_ARRAY_DELETE(T, g_b39DtorCount, g_b39DtorOrder);
}

// The nine slots of the six resources, by PMF (adjustment 0).
template <typename C>
void B39ExpectNineSlots() {
    CHECK_DTOR_SLOT(C);
    CHECK_SLOT(C, TypeName,       kTypeName/8);
    CHECK_SLOT(C, Type,           kType/8);
    CHECK_SLOT(C, GetFileVersion, kGetFileVersion/8);
    CHECK_SLOT(C, Serialize,      kSerialize/8);
    CHECK_SLOT(C, Reconstruct,    kReconstruct/8);
    CHECK_SLOT(C, Destruct,       kDestruct/8);
    CHECK_SLOT(C, Construct,      kConstruct/8);
}

UNIT_TEST(b39_uid_characterset_slot_order_by_pmf) {
    B39ExpectNineSlots<UI::CharacterSet>();
}

// The inlined constructor (Construct 0x1800376b0): External's three words and
// +0x20 zeroed; the compiler's table, one per class, not the probe's.
UNIT_TEST(b39_uid_characterset_ctor_makes_retails_stores) {
    alignas(8) uint8_t buf[0x28];
    std::memset(buf, 0xCD, sizeof buf);
    UI::CharacterSet* c = ::new (buf) UI::CharacterSet();
    CHECK(static_cast<void*>(c) == static_cast<void*>(buf));
    for (int off = 0x08; off < 0x28; off += 8)
        CHECK(*reinterpret_cast<void**>(buf + off) == nullptr);
    alignas(8) uint8_t buf2[0x28];
    UI::CharacterSet* c2 = ::new (buf2) UI::CharacterSet();
    CharacterSetProbe probe;
    CHECK(VTableOf(c) == VTableOf(c2));
    CHECK(VTableOf(c) != VTableOf(&probe));
    g_b39DtorCount = 0;
    c->~CharacterSet();
    c2->~CharacterSet();
    CHECK_EQ(g_b39DtorCount, 2);
}

// Retired vector arms (the retail slot-0 bodies): CharacterSet 0x1800378a0,
// Cursor 0x18003b3a0, StringTable 0x180002740, Font 0x180032130.
UNIT_TEST(b39_uid_characterset_deleting_dtor_both_arms) {
    B39ExpectBothArms<UI::CharacterSet>(0x28, [](void* p) { return ::new (p) UI::CharacterSet(); });
}

UNIT_TEST(b39_uid_cursor_slot_order_by_pmf) {
    B39ExpectNineSlots<UI::Cursor>();
}

// The inlined constructor (Construct 0x18003b1e0): External's three words and
// +0x28 zeroed; +0x20 / +0x24 LEFT ALONE (retail's).
UNIT_TEST(b39_uid_cursor_ctor_makes_retails_stores) {
    alignas(8) uint8_t buf[0x30];
    std::memset(buf, 0xCD, sizeof buf);
    UI::Cursor* c = ::new (buf) UI::Cursor();
    CHECK(static_cast<void*>(c) == static_cast<void*>(buf));
    for (int off = 0x08; off < 0x20; off += 8)
        CHECK(*reinterpret_cast<void**>(buf + off) == nullptr);
    CHECK_EQ(*reinterpret_cast<uint32_t*>(buf + 0x20), 0xCDCDCDCDu);
    CHECK_EQ(*reinterpret_cast<uint32_t*>(buf + 0x24), 0xCDCDCDCDu);
    CHECK(*reinterpret_cast<void**>(buf + 0x28) == nullptr);
    CursorProbe probe;
    CHECK(VTableOf(c) != VTableOf(&probe));
    g_b39DtorCount = 0;
    c->~Cursor();
    CHECK_EQ(g_b39DtorCount, 1);
}

UNIT_TEST(b39_uid_cursor_deleting_dtor_both_arms) {
    B39ExpectBothArms<UI::Cursor>(0x30, [](void* p) { return ::new (p) UI::Cursor(); });
}

UNIT_TEST(b39_uid_spritecapture_slot_order_by_pmf) {
    B39ExpectNineSlots<UI::SpriteCapture>();
    CHECK(std::is_destructible<UI::SpriteCapture>::value);    // B3X W1: public virtual dtor
    CHECK(std::has_virtual_destructor<UI::SpriteCapture>::value);
}

UNIT_TEST(b39_uid_stringtable_slot_order_by_pmf) {
    B39ExpectNineSlots<UI::StringTable>();
}

// ??0StringTable 0x18011d890: External's three words, +0x20 and +0x28 zeroed.
UNIT_TEST(b39_uid_stringtable_ctor_makes_retails_stores) {
    alignas(8) uint8_t buf[0x30];
    std::memset(buf, 0xCD, sizeof buf);
    UI::StringTable* t = ::new (buf) UI::StringTable();
    CHECK(static_cast<void*>(t) == static_cast<void*>(buf));
    for (int off = 0x08; off < 0x30; off += 8)
        CHECK(*reinterpret_cast<void**>(buf + off) == nullptr);
    StringTableProbe probe;
    CHECK(VTableOf(t) != VTableOf(&probe));
    g_b39DtorCount = 0;
    t->~StringTable();
    CHECK_EQ(g_b39DtorCount, 1);
}

UNIT_TEST(b39_uid_stringtable_deleting_dtor_both_arms) {
    B39ExpectBothArms<UI::StringTable>(0x30, [](void* p) { return ::new (p) UI::StringTable(); });
}

UNIT_TEST(b39_uid_font_slot_order_by_pmf) {
    B39ExpectNineSlots<UI::Font>();
    CHECK_SLOT(UI::Font, Compile, kCompile/8);   // the override
}

// The inlined constructor (Construct 0x180031ce0, 21 stores): +0x08..+0x58
// zero, +0x5c NEVER written, +0x60..+0x88 zero.
UNIT_TEST(b39_uid_font_ctor_makes_retails_stores) {
    alignas(8) uint8_t buf[0x90];
    std::memset(buf, 0xCD, sizeof buf);
    UI::Font* f = ::new (buf) UI::Font();
    CHECK(static_cast<void*>(f) == static_cast<void*>(buf));
    for (int off = 0x08; off < 0x58; off += 8)
        CHECK(*reinterpret_cast<void**>(buf + off) == nullptr);
    CHECK_EQ(*reinterpret_cast<uint32_t*>(buf + 0x58), 0u);
    CHECK_EQ(*reinterpret_cast<uint32_t*>(buf + 0x5c), 0xCDCDCDCDu);
    for (int off = 0x60; off < 0x90; off += 8)
        CHECK(*reinterpret_cast<void**>(buf + off) == nullptr);
    FontProbe probe;
    CHECK(VTableOf(f) != VTableOf(&probe));
    g_b39DtorCount = 0;
    f->~Font();
    CHECK_EQ(g_b39DtorCount, 1);
}

UNIT_TEST(b39_uid_font_deleting_dtor_both_arms) {
    B39ExpectBothArms<UI::Font>(0x90, [](void* p) { return ::new (p) UI::Font(); });
}

}  // namespace

// ############################################################################
// MARKER bodies (see the banner). GetFileVersion answers retail's literal.
// ############################################################################
namespace UI {

int32_t     CharacterSet::GetFileVersion() const { Note(this, kGetFileVersion / 8); return 1; }
bool        CharacterSet::Serialize(IO::Stream&) { Note(this, kSerialize / 8); return true; }
Object::External* CharacterSet::Reconstruct() { Note(this, kReconstruct / 8); return this; }
void        CharacterSet::Destruct() { Note(this, kDestruct / 8); }
Object::External* CharacterSet::Construct() { Note(this, kConstruct / 8); return this; }
CharacterSet::~CharacterSet() { B39NoteDtor(this); }

int32_t     Cursor::GetFileVersion() const { Note(this, kGetFileVersion / 8); return 1; }
bool        Cursor::Serialize(IO::Stream&) { Note(this, kSerialize / 8); return true; }
Object::External* Cursor::Reconstruct() { Note(this, kReconstruct / 8); return this; }
void        Cursor::Destruct() { Note(this, kDestruct / 8); }
Object::External* Cursor::Construct() { Note(this, kConstruct / 8); return this; }
Cursor::~Cursor() { B39NoteDtor(this); }

int32_t     StringTable::GetFileVersion() const { Note(this, kGetFileVersion / 8); return 1; }
bool        StringTable::Serialize(IO::Stream&) { Note(this, kSerialize / 8); return true; }
Object::External* StringTable::Reconstruct() { Note(this, kReconstruct / 8); return this; }
void        StringTable::Destruct() { Note(this, kDestruct / 8); }
Object::External* StringTable::Construct() { Note(this, kConstruct / 8); return this; }
StringTable::~StringTable() { B39NoteDtor(this); }

int32_t     SpriteSheet::GetFileVersion() const { Note(this, kGetFileVersion / 8); return 6; }
// The OVERRIDE of slot 4; answers retail's 1 (0x18011d720).
Object::External::CompileResult SpriteSheet::Compile(const System::Filename&) {
    Note(this, kCompile / 8); return 1u;
}
bool        SpriteSheet::Serialize(IO::Stream&) { Note(this, kSerialize / 8); return true; }
Object::External* SpriteSheet::Reconstruct() { Note(this, kReconstruct / 8); return this; }
void        SpriteSheet::Destruct() { Note(this, kDestruct / 8); }
Object::External* SpriteSheet::Construct() { Note(this, kConstruct / 8); return this; }
SpriteSheet::~SpriteSheet() {}

int32_t     SpriteCapture::GetFileVersion() const { Note(this, kGetFileVersion / 8); return 1; }
bool        SpriteCapture::Serialize(IO::Stream&) { Note(this, kSerialize / 8); return true; }
Object::External* SpriteCapture::Reconstruct() { Note(this, kReconstruct / 8); return this; }
void        SpriteCapture::Destruct() { Note(this, kDestruct / 8); }
Object::External* SpriteCapture::Construct() { Note(this, kConstruct / 8); return this; }
SpriteCapture::~SpriteCapture() {}

int32_t     Font::GetFileVersion() const { Note(this, kGetFileVersion / 8); return 8; }
// The OVERRIDE of slot 4; answers retail's 1 (0x1801176c0).
Object::External::CompileResult Font::Compile(const System::Filename&) {
    Note(this, kCompile / 8); return 1u;
}
bool        Font::Serialize(IO::Stream&) { Note(this, kSerialize / 8); return true; }
Object::External* Font::Reconstruct() { Note(this, kReconstruct / 8); return this; }
void        Font::Destruct() { Note(this, kDestruct / 8); }
Object::External* Font::Construct() { Note(this, kConstruct / 8); return this; }
Font::~Font() { B39NoteDtor(this); }

}  // namespace UI

// ENDGAME B.3.9 TI-UI: the six records (literals as src/engine/ui passes them)
// and the null factory pair they point at; never called here.
#define RES_TEST_FACTORIES(CLS)                                             \
    ::Object::Base* UI::CLS::CreateNewObject() { return nullptr; }              \
    ::Object::Base* UI::CLS::CreateInPlaceObject(void*) { return nullptr; }
RES_TEST_FACTORIES(CharacterSet)
DEFINE_TYPE(UI::CharacterSet, "Object::External", nullptr)
RES_TEST_FACTORIES(Cursor)
DEFINE_TYPE(UI::Cursor, "Object::External", nullptr)
RES_TEST_FACTORIES(StringTable)
DEFINE_TYPE(UI::StringTable, "Object::External", nullptr)
RES_TEST_FACTORIES(SpriteSheet)
DEFINE_TYPE(UI::SpriteSheet, "Object::External", nullptr)
RES_TEST_FACTORIES(SpriteCapture)
DEFINE_TYPE(UI::SpriteCapture, "Object::External", nullptr)
RES_TEST_FACTORIES(Font)
DEFINE_TYPE(UI::Font, "Object::External", nullptr)
#undef RES_TEST_FACTORIES
