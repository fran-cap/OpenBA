// ============================================================================
// tests/unit/ui_leaf_c3_abi_test.cpp — ENDGAME B.3 step 6, block C3
//   Text / Label / Edit / NumberEdit / Number, both sides, as real C++ classes
//
//   UI::TextDescription       : ElementDescription  0x90  10 slots
//   UI::LabelDescription      : TextDescription     0xb8  10 slots
//   UI::EditDescription       : ElementDescription  0x88  10 slots
//   UI::NumberEditDescription : ElementDescription  0x88  10 slots (NOT Edit's)
//   UI::NumberDescription     : ElementDescription  0x88  10 slots
//   UI::TextElement           : Element             0x100 12 slots
//   UI::LabelElement          : TextElement         0x130 12 slots
//   UI::EditElement           : Element             0xf8  13 slots (introduces 12)
//   UI::NumberEditElement     : EditElement         0x100 13 slots
//   UI::NumberElement         : Element             0xe8  12 slots
//
// Built to the contract of ui_element_abi_test.cpp (block A), whose file
// defines `UI::Element` / `UI::ElementDescription`'s out-of-line members as
// markers — that is what makes the base vtables link here. This file defines
// its ten classes' own out-of-line virtuals AS MARKERS the same way
// (src/engine/ui cannot link into a host test).
//
// PART A — LAYOUT: sizes, alignments, the parents (the `_ParentTypeName`
//   literals), every offset the headers claim, class vs POD.
// PART B — SLOT ORDER: probe subclasses read back BY BYTE OFFSET and raw-called;
//   the marker that runs names the member. Slots a class INHERITS are proved
//   by pointer identity with the parent's probe (their markers live in block
//   A's TU and report to its own channel).
// PART C — ABI: slot 8 `CreateInstance` carries the hidden return slot
//   (B6-2); `EditElement::Update` receives a POINTER to the handle (B6-11).
// PART D — the QUALIFIED call on the slot this block introduces (12).
// ============================================================================
#include "unit.h"
#include "slot_check.h"
#include "dtor_check.h"

#include "ui/TextDescription.h"
#include "ui/LabelDescription.h"
#include "ui/EditDescription.h"
#include "ui/NumberEditDescription.h"
#include "ui/NumberDescription.h"
#include "ui/TextElement.h"
#include "ui/LabelElement.h"
#include "ui/EditElement.h"
#include "ui/NumberEditElement.h"
#include "ui/NumberElement.h"
#include "ui/DialogInstance.h"   // B4 S4 RT2: InternalPtr<DialogInstance>::Get() needs the class complete

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <new>
#include <type_traits>

namespace Object {
namespace detail {
extern InternalFactory* g_unitInternalFactory;
extern uint32_t (*g_unitInternalRandHook)(System::Random*);
}  // namespace detail
}  // namespace Object

namespace {

const void* g_lastSelf = nullptr;
int         g_lastMark = -1;
long long   g_lastArg  = 0;

void Note(const void* self, int mark, long long arg = 0) {
    g_lastSelf = self; g_lastMark = mark; g_lastArg = arg;
}
void Reset() { g_lastSelf = nullptr; g_lastMark = -1; g_lastArg = 0; }

// Slots 1/2 (ENDGAME B.3.9 TI-UI): the record of the class `tag` names.
const ::Object::BaseTypeInfo* WhoRecord(int tag);

// The offset / index is RETAIL's; T decides the compiler's. Default T: every object this file reads raw is a UI class, all Object::Base-rooted
// (a virtual destructor is inherited, so the root answers for every class).
template <typename Fn, class T = Object::Base>
Fn SlotAt(const void* obj, int byteOffset) {
    const uint8_t* vt = *reinterpret_cast<const uint8_t* const*>(obj);
    Fn f = nullptr;
    const void* raw = *reinterpret_cast<const void* const*>(vt + RetailByteOffset<T>(byteOffset));
    std::memcpy(&f, &raw, sizeof f);
    return f;
}
// Retail slot n of a table in this file, as the compiler numbers it.
constexpr int32_t Retail(int n) { return RetailSlot<Object::Base>(n); }
const void* const* VTableOf(const void* obj) {
    return *reinterpret_cast<const void* const* const*>(obj);
}

uint32_t RandHook(System::Random*) { return 7u; }

struct FactoryScope {
    ::Object::InternalFactory  factory;
    ::Object::InternalFactory* saved;
    uint32_t (*savedRand)(System::Random*);
    FactoryScope()
        : factory(),
          saved(::Object::detail::g_unitInternalFactory),
          savedRand(::Object::detail::g_unitInternalRandHook) {
        ::Object::detail::g_unitInternalFactory  = &factory;
        ::Object::detail::g_unitInternalRandHook = &RandHook;
    }
    ~FactoryScope() {
        ::Object::detail::g_unitInternalFactory  = saved;
        ::Object::detail::g_unitInternalRandHook = savedRand;
    }
};

// Class tags: mark = tag * 100 + slot index.
enum : int {
    kTD = 1, kLD = 2, kED = 3, kNED = 4, kND = 5,
    kTE = 6, kLE = 7, kEE = 8, kNEE = 9, kNE = 10,
};
constexpr int kProbeDtor = 99;
constexpr int kProbeFinalize = 98;

// Byte offsets (B36_SLOTS.md section 1).
enum : int {
    kS0 = 0x00, kS1 = 0x08, kS2 = 0x10, kS3 = 0x18, kS4 = 0x20, kS5 = 0x28,
    kS6 = 0x30, kS7 = 0x38, kS8 = 0x40, kS9 = 0x48, kS10 = 0x50, kS11 = 0x58,
    kS12 = 0x60,
};

// ---- probes: each overrides only retail slot 0 ------------------------------
// B3X W1: retail slot 0 is the destructor pair; the probe's destructor records
// kProbeDtor, and a recording class-scope operator delete stands for retail
// flags 1's MemFree.
#define C3_PROBE(NAME, BASE)                                                   \
    struct NAME final : BASE {                                                 \
        ~NAME() override { Note(this, kProbeDtor); }                           \
        DTOR_CHECK_RECORDING_DELETE                                            \
    };
C3_PROBE(TextDescriptionProbe,       UI::TextDescription)
C3_PROBE(LabelDescriptionProbe,      UI::LabelDescription)
C3_PROBE(EditDescriptionProbe,       UI::EditDescription)
C3_PROBE(NumberEditDescriptionProbe, UI::NumberEditDescription)
C3_PROBE(NumberDescriptionProbe,     UI::NumberDescription)
C3_PROBE(TextElementProbe,           UI::TextElement)
C3_PROBE(LabelElementProbe,          UI::LabelElement)
C3_PROBE(EditElementProbe,           UI::EditElement)
C3_PROBE(NumberEditElementProbe,     UI::NumberEditElement)
C3_PROBE(NumberElementProbe,         UI::NumberElement)
#undef C3_PROBE

// A probe that overrides the slot EditElement INTRODUCES, for PART D.
struct EditFinalizeProbe final : UI::EditElement {
    ~EditFinalizeProbe() override { Note(this, kProbeDtor); }
    void FinalizeText() override { Note(this, kProbeFinalize); }
};

void* const kRetSentinel = reinterpret_cast<void*>(static_cast<uintptr_t>(0xE1E1E1E1));

}  // namespace

// ENDGAME B.3.9 UI-A: the destructor markers RECORD each call, so a concrete
// class's destructors (B3X W1: D1 / D0 / delete[]) can be seen running ~X
// once per element, in retail's DESCENDING order.
namespace {
int         g_b39DtorCount = 0;
const void* g_b39DtorOrder[8] = {};
void B39NoteDtor(const void* self) {
    if (g_b39DtorCount < 8) g_b39DtorOrder[g_b39DtorCount] = self;
    ++g_b39DtorCount;
}
}  // namespace

// ===========================================================================
// The ten classes' out-of-line members, AS MARKERS.
// ===========================================================================
namespace UI {

#define C3_DESC_MARKERS(CLS, TAG)                                                     \
    int32_t CLS::GetFileVersion() const { Note(this, TAG * 100 + 3); return TAG; }    \
    bool CLS::Serialize(IO::Stream& s) {                                                    \
        Note(this, TAG * 100 + 5, reinterpret_cast<intptr_t>(&s)); return true;       \
    }                                                                                 \
    Object::External* CLS::Reconstruct() { Note(this, TAG * 100 + 6); return this; }  \
    void CLS::Destruct() { Note(this, TAG * 100 + 7); }                               \
    Object::InternalPtr<Element>* CLS::CreateInstance(                                \
            Object::InternalPtr<Element>* ret, const Dialog* dialog) const {         \
        Note(this, TAG * 100 + 8, reinterpret_cast<intptr_t>(dialog));               \
        *reinterpret_cast<void**>(ret) = kRetSentinel;                               \
        return ret;                                                                   \
    }                                                                                 \
    Object::External* CLS::Construct() { Note(this, TAG * 100 + 9); return this; }    \
    CLS::~CLS() { B39NoteDtor(this); }

C3_DESC_MARKERS(TextDescription, kTD)
C3_DESC_MARKERS(LabelDescription, kLD)
C3_DESC_MARKERS(EditDescription, kED)
C3_DESC_MARKERS(NumberEditDescription, kNED)
C3_DESC_MARKERS(NumberDescription, kND)
#undef C3_DESC_MARKERS

#define C3_ELEM_TYPE_MARKERS(CLS, TAG)                                                \
    CLS::~CLS() { B39NoteDtor(this); }
#define C3_ELEM_LAYOUT_MARKERS(CLS, TAG)                                              \
    void CLS::ComputeSize() { Note(this, TAG * 100 + 4); }                            \
    void CLS::ComputeLayout(int32_t x, int32_t, int32_t, int32_t h) {                 \
        Note(this, TAG * 100 + 5, (long long)x * 1000 + h);                           \
    }                                                                                 \
    void CLS::Draw(SpriteContext&, uint8_t alpha) const { Note(this, TAG * 100 + 6, alpha); }

C3_ELEM_TYPE_MARKERS(TextElement, kTE)
C3_ELEM_LAYOUT_MARKERS(TextElement, kTE)
C3_ELEM_TYPE_MARKERS(LabelElement, kLE)
C3_ELEM_LAYOUT_MARKERS(LabelElement, kLE)
void LabelElement::SetColor(uint32_t color) { Note(this, kLE * 100 + 10, color); }
C3_ELEM_TYPE_MARKERS(EditElement, kEE)
C3_ELEM_LAYOUT_MARKERS(EditElement, kEE)
void EditElement::Update(const System::Time&, UserInterface*,
                         Object::InternalPtr<DialogInstance>* dialog,
                         const UpdateState& state) {
    // The HANDLE the member saw through the pointer, plus mouseX (B6-11).
    Note(this, kEE * 100 + 7,
         static_cast<long long>(reinterpret_cast<intptr_t>(dialog->Get())) + state.mouseX);
}
void EditElement::FinalizeText() { Note(this, kEE * 100 + 12); }
C3_ELEM_TYPE_MARKERS(NumberEditElement, kNEE)
void NumberEditElement::FinalizeText() { Note(this, kNEE * 100 + 12); }
C3_ELEM_TYPE_MARKERS(NumberElement, kNE)
C3_ELEM_LAYOUT_MARKERS(NumberElement, kNE)
// B.3.9 UI-A: the real NumberElement ctor calls its private CacheText (retail
// ??0NumberElement 0x180118c70 does), so the host needs a body: a marker.
int g_numberCacheTextCalls = 0;
void NumberElement::CacheText() { ++g_numberCacheTextCalls; }
// Likewise the real TextElement ctor (retail ??0TextElement 0x18011eb80) ends
// in its private CacheText: a marker.
int g_textCacheTextCalls = 0;
void TextElement::CacheText() { ++g_textCacheTextCalls; }
#undef C3_ELEM_TYPE_MARKERS
#undef C3_ELEM_LAYOUT_MARKERS

}  // namespace UI

// ENDGAME B.3.9 TI-UI: slots 1/2 are DECLARE_TYPE's inline bodies over each
// class's own record, so the host defines the records (the literals are the
// ones src/engine/ui passes) and the factory pair they point at; the
// factories are never called here.
#define C3_TEST_FACTORIES(CLS)                                              \
    ::Object::Base* UI::CLS::CreateNewObject() { return nullptr; }              \
    ::Object::Base* UI::CLS::CreateInPlaceObject(void*) { return nullptr; }
C3_TEST_FACTORIES(TextDescription)
DEFINE_TYPE(UI::TextDescription, "ElementDescription", nullptr)
C3_TEST_FACTORIES(LabelDescription)
DEFINE_TYPE(UI::LabelDescription, "TextDescription", nullptr)
C3_TEST_FACTORIES(EditDescription)
DEFINE_TYPE(UI::EditDescription, "ElementDescription", nullptr)
C3_TEST_FACTORIES(NumberEditDescription)
DEFINE_TYPE(UI::NumberEditDescription, "ElementDescription", nullptr)
C3_TEST_FACTORIES(NumberDescription)
DEFINE_TYPE(UI::NumberDescription, "ElementDescription", nullptr)
C3_TEST_FACTORIES(TextElement)
DEFINE_TYPE(UI::TextElement, "Element", nullptr)
C3_TEST_FACTORIES(LabelElement)
DEFINE_TYPE(UI::LabelElement, "TextElement", nullptr)
C3_TEST_FACTORIES(EditElement)
DEFINE_TYPE(UI::EditElement, "Element", nullptr)
C3_TEST_FACTORIES(NumberEditElement)
DEFINE_TYPE(UI::NumberEditElement, "EditElement", nullptr)
C3_TEST_FACTORIES(NumberElement)
DEFINE_TYPE(UI::NumberElement, "Element", nullptr)
#undef C3_TEST_FACTORIES

namespace {
const ::Object::BaseTypeInfo* WhoRecord(int tag) {
    switch (tag) {
    case kTD: return &UI::TextDescription::GetTypeInfo();
    case kLD: return &UI::LabelDescription::GetTypeInfo();
    case kED: return &UI::EditDescription::GetTypeInfo();
    case kNED: return &UI::NumberEditDescription::GetTypeInfo();
    case kND: return &UI::NumberDescription::GetTypeInfo();
    case kTE: return &UI::TextElement::GetTypeInfo();
    case kLE: return &UI::LabelElement::GetTypeInfo();
    case kEE: return &UI::EditElement::GetTypeInfo();
    case kNEE: return &UI::NumberEditElement::GetTypeInfo();
    case kNE: return &UI::NumberElement::GetTypeInfo();
    default: return nullptr;
    }
}
}  // namespace

// ===========================================================================
// PART A — LAYOUT
// ===========================================================================
UNIT_TEST(ui_c3_sizes_and_parents_match_retail) {
    // `MOV EDX,imm` in each class's slot-0 vector deleting destructor.
    CHECK_EQ(alignof(UI::EditElement), (size_t)8);
    CHECK_EQ(alignof(UI::LabelElement), (size_t)8);
    CHECK_EQ(alignof(UI::NumberDescription), (size_t)8);

    // The parents, as the `_ParentTypeName` literals say.
    CHECK((std::is_base_of<UI::ElementDescription, UI::TextDescription>::value));
    CHECK((std::is_base_of<UI::TextDescription, UI::LabelDescription>::value));
    CHECK((std::is_base_of<UI::ElementDescription, UI::EditDescription>::value));
    CHECK((std::is_base_of<UI::ElementDescription, UI::NumberEditDescription>::value));
    CHECK(!(std::is_base_of<UI::EditDescription, UI::NumberEditDescription>::value));
    CHECK((std::is_base_of<UI::ElementDescription, UI::NumberDescription>::value));
    CHECK((std::is_base_of<UI::Element, UI::TextElement>::value));
    CHECK((std::is_base_of<UI::TextElement, UI::LabelElement>::value));
    CHECK((std::is_base_of<UI::Element, UI::EditElement>::value));
    CHECK((std::is_base_of<UI::EditElement, UI::NumberEditElement>::value));
    CHECK((std::is_base_of<UI::Element, UI::NumberElement>::value));
    CHECK(!(std::is_base_of<UI::TextElement, UI::NumberElement>::value));

    // Concrete since B.3.9 UI-A; public virtual destructor since B3X W1.
    CHECK(!std::is_abstract<UI::TextDescription>::value);    // B.3.9 UI-A: concrete
    CHECK(!std::is_abstract<UI::LabelElement>::value);       // B.3.9 UI-A: concrete
    CHECK(!std::is_abstract<UI::TextElement>::value);
    CHECK(!std::is_abstract<UI::LabelDescription>::value);
    for (bool destructible : { std::is_destructible<UI::TextDescription>::value,
                               std::is_destructible<UI::TextElement>::value,
                               std::is_destructible<UI::LabelDescription>::value,
                               std::is_destructible<UI::LabelElement>::value })
        CHECK(destructible);                                   // B3X W1: public virtual dtors
    CHECK(!std::is_abstract<UI::NumberEditElement>::value);   // B.3.9 UI-A: concrete
    for (bool destructible : { std::is_destructible<UI::EditDescription>::value,
                               std::is_destructible<UI::EditElement>::value,
                               std::is_destructible<UI::NumberEditDescription>::value,
                               std::is_destructible<UI::NumberEditElement>::value,
                               std::is_destructible<UI::NumberDescription>::value,
                               std::is_destructible<UI::NumberElement>::value })
        CHECK(destructible);                                   // B3X W1: public virtual dtors
    CHECK(!std::is_abstract<UI::EditDescription>::value);
    CHECK(!std::is_abstract<UI::EditElement>::value);
    CHECK(!std::is_abstract<UI::NumberEditDescription>::value);
    CHECK(!std::is_abstract<UI::NumberDescription>::value);
    CHECK(!std::is_abstract<UI::NumberElement>::value);
    CHECK(std::has_virtual_destructor<UI::TextElement>::value);            // B3X W1
    CHECK(std::has_virtual_destructor<UI::NumberEditDescription>::value);
}

// ===========================================================================
// PART B — SLOT ORDER
// ===========================================================================
namespace {

// Retail slot 0 (kS0) is the destructor pair (B3X W1), read raw: compiler [0]
// D1 runs ~Probe once and frees nothing (flags 0), on a placement-new'd probe
// that is never destroyed again (a stack probe would be destroyed twice); [1]
// D0 runs it and hands the block to the recording operator delete (flags 1).
template <typename Probe>
void CheckProbeDtorPair() {
    static_assert(kS0 == 0, "retail slot 0");
    alignas(Probe) unsigned char buf[sizeof(Probe)];
    Probe* p = ::new (static_cast<void*>(buf)) Probe();
    CHECK(VTableOf(p)[0] != VTableOf(p)[1]);
    Reset(); dtor_check::ResetFrees();
    dtor_check::CallD1(p);
    CHECK_EQ(g_lastMark, kProbeDtor); CHECK(g_lastSelf == p);
    CHECK_EQ(dtor_check::g_frees, 0);
    Probe* h = dtor_check::NewOnMalloc<Probe>();
    Reset(); dtor_check::ResetFrees();
    dtor_check::CallD0(h);
    CHECK_EQ(g_lastMark, kProbeDtor); CHECK(g_lastSelf == h);
    CHECK_EQ(dtor_check::g_frees, 1); CHECK(dtor_check::g_lastFree == h);
}

// Slots 1,2,3,5,6,7,8,9 of a leaf Description reach the class's own markers,
// slot 0 the probe (CheckProbeDtorPair), slot 4 `Object::External::Compile`
// (inherited).
void CheckDescriptionSlots(void* raw, int tag, const void* compileEntry) {
    CHECK(VTableOf(raw)[0] != VTableOf(raw)[1]);   // D1 != D0
    CHECK(SlotAt<const char* (*)(const void*)>(raw, kS1)(raw) == WhoRecord(tag)->name);
    CHECK_EQ(SlotAt<uint32_t (*)(const void*)>(raw, kS2)(raw), WhoRecord(tag)->typeId);
    Reset(); SlotAt<int32_t (*)(const void*)>(raw, kS3)(raw);
    CHECK_EQ(g_lastMark, tag * 100 + 3);
    CHECK(RetailEntry<Object::Base>(raw, kS4 / 8) == compileEntry);
    Reset(); SlotAt<bool (*)(void*, void*)>(raw, kS5)(raw, (void*)0x55);
    CHECK_EQ(g_lastMark, tag * 100 + 5); CHECK_EQ(g_lastArg, 0x55);
    Reset(); SlotAt<void* (*)(void*)>(raw, kS6)(raw);
    CHECK_EQ(g_lastMark, tag * 100 + 6);
    Reset(); SlotAt<void (*)(void*)>(raw, kS7)(raw);
    CHECK_EQ(g_lastMark, tag * 100 + 7);
    void* slot = nullptr;
    Reset(); SlotAt<void** (*)(const void*, void**, const void*)>(raw, kS8)(raw, &slot, nullptr);
    CHECK_EQ(g_lastMark, tag * 100 + 8);
    // RULE B6-1.1: Construct is slot 9, BEHIND CreateInstance.
    Reset(); SlotAt<void* (*)(void*)>(raw, kS9)(raw);
    CHECK_EQ(g_lastMark, tag * 100 + 9);
}

}  // namespace

UNIT_TEST(ui_c3_description_slot_order_is_retails) {
    TextDescriptionProbe td;
    LabelDescriptionProbe ld;
    EditDescriptionProbe ed;
    NumberEditDescriptionProbe ned;
    NumberDescriptionProbe nd;

    // Every one inherits the same External::Compile at slot 4 (B6-1.5).
    const void* compile = RetailEntry<Object::Base>(&td, kS4 / 8);
    CheckDescriptionSlots(&td, kTD, compile);
    CheckDescriptionSlots(&ld, kLD, compile);
    CheckDescriptionSlots(&ed, kED, compile);
    CheckDescriptionSlots(&ned, kNED, compile);
    CheckDescriptionSlots(&nd, kND, compile);
    CheckProbeDtorPair<TextDescriptionProbe>();
    CheckProbeDtorPair<LabelDescriptionProbe>();
    CheckProbeDtorPair<EditDescriptionProbe>();
    CheckProbeDtorPair<NumberEditDescriptionProbe>();
    CheckProbeDtorPair<NumberDescriptionProbe>();

    // Label OVERRIDES all of Text's 1..9 but 4: none of its entries is Text's.
    for (int s = 1; s < 10; ++s) {
        if (s == 4) continue;
        CHECK(VTableOf(&ld)[Retail(s)] != VTableOf(&td)[Retail(s)]);
    }
    CHECK(static_cast<void*>(static_cast<UI::TextDescription*>(&ld)) == static_cast<void*>(&ld));
}

UNIT_TEST(ui_c3_text_and_label_element_slot_order_is_retails) {
    FactoryScope scope;
    TextElementProbe te{};
    LabelElementProbe le{};
    void* t = static_cast<void*>(&te);
    void* l = static_cast<void*>(&le);

    CHECK(SlotAt<const char* (*)(const void*)>(t, kS1)(t) == WhoRecord(kTE)->name);
    CHECK_EQ(SlotAt<uint32_t (*)(const void*)>(t, kS2)(t), WhoRecord(kTE)->typeId);
    Reset(); SlotAt<void (*)(void*)>(t, kS4)(t);              CHECK_EQ(g_lastMark, kTE * 100 + 4);
    Reset(); SlotAt<void (*)(void*, int, int, int, int)>(t, kS5)(t, 7, 0, 0, 9);
    CHECK_EQ(g_lastMark, kTE * 100 + 5); CHECK_EQ(g_lastArg, 7009);
    Reset(); SlotAt<void (*)(const void*, void*, uint8_t)>(t, kS6)(t, nullptr, 200);
    CHECK_EQ(g_lastMark, kTE * 100 + 6); CHECK_EQ(g_lastArg, 200);

    // Label overrides 1,2,4,5,6 and SetColor (10).
    CHECK(SlotAt<const char* (*)(const void*)>(l, kS1)(l) == WhoRecord(kLE)->name);
    Reset(); SlotAt<void (*)(void*)>(l, kS4)(l);              CHECK_EQ(g_lastMark, kLE * 100 + 4);
    Reset(); SlotAt<void (*)(void*, int, int, int, int)>(l, kS5)(l, 1, 0, 0, 2);
    CHECK_EQ(g_lastMark, kLE * 100 + 5);
    Reset(); SlotAt<void (*)(const void*, void*, uint8_t)>(l, kS6)(l, nullptr, 9);
    CHECK_EQ(g_lastMark, kLE * 100 + 6);
    Reset(); SlotAt<void (*)(void*, uint32_t)>(l, kS10)(l, 0xABCDu);
    CHECK_EQ(g_lastMark, kLE * 100 + 10); CHECK_EQ(g_lastArg, 0xABCD);

    // ...and INHERITS 3, 7, 8, 9, 11 through TextElement (Element's bodies).
    const int inherited[] = { 3, 7, 8, 9, 11 };
    for (int s : inherited) CHECK(VTableOf(l)[Retail(s)] == VTableOf(t)[Retail(s)]);
    // Text does NOT override SetColor; Label does.
    CHECK(VTableOf(l)[Retail(10)] != VTableOf(t)[Retail(10)]);
}

UNIT_TEST(ui_c3_edit_and_numberedit_element_slot_order_is_retails) {
    FactoryScope scope;
    EditElementProbe ee{};
    NumberEditElementProbe ne{};
    TextElementProbe te{};
    void* e = static_cast<void*>(&ee);
    void* n = static_cast<void*>(&ne);

    CHECK(SlotAt<const char* (*)(const void*)>(e, kS1)(e) == WhoRecord(kEE)->name);
    Reset(); SlotAt<void (*)(void*)>(e, kS4)(e);              CHECK_EQ(g_lastMark, kEE * 100 + 4);
    Reset(); SlotAt<void (*)(void*, int, int, int, int)>(e, kS5)(e, 3, 0, 0, 4);
    CHECK_EQ(g_lastMark, kEE * 100 + 5);
    Reset(); SlotAt<void (*)(const void*, void*, uint8_t)>(e, kS6)(e, nullptr, 1);
    CHECK_EQ(g_lastMark, kEE * 100 + 6);
    // SLOT 12 — introduced by EditElement (B6-1.6): thirteen slots.
    Reset(); SlotAt<void (*)(void*)>(e, kS12)(e);             CHECK_EQ(g_lastMark, kEE * 100 + 12);

    // NumberEdit: own 1, 2 and 12; INHERITS 4..7 from EditElement.
    CHECK(SlotAt<const char* (*)(const void*)>(n, kS1)(n) == WhoRecord(kNEE)->name);
    CHECK_EQ(SlotAt<uint32_t (*)(const void*)>(n, kS2)(n), WhoRecord(kNEE)->typeId);
    Reset(); SlotAt<void (*)(void*)>(n, kS12)(n);             CHECK_EQ(g_lastMark, kNEE * 100 + 12);
    for (int s = 3; s <= 11; ++s) CHECK(VTableOf(n)[Retail(s)] == VTableOf(e)[Retail(s)]);
    Reset(); SlotAt<void (*)(void*)>(n, kS4)(n);              CHECK_EQ(g_lastMark, kEE * 100 + 4);

    // Edit overrides Update (7); Text inherits Element's.
    CHECK(VTableOf(e)[Retail(7)] != VTableOf(&te)[Retail(7)]);
    CHECK(VTableOf(e)[Retail(8)] == VTableOf(&te)[Retail(8)]);
}

UNIT_TEST(ui_c3_number_element_slot_order_is_retails) {
    FactoryScope scope;
    NumberElementProbe np{};
    TextElementProbe te{};
    void* n = static_cast<void*>(&np);
    CHECK(SlotAt<const char* (*)(const void*)>(n, kS1)(n) == WhoRecord(kNE)->name);
    CHECK_EQ(SlotAt<uint32_t (*)(const void*)>(n, kS2)(n), WhoRecord(kNE)->typeId);
    Reset(); SlotAt<void (*)(void*)>(n, kS4)(n);              CHECK_EQ(g_lastMark, kNE * 100 + 4);
    Reset(); SlotAt<void (*)(void*, int, int, int, int)>(n, kS5)(n, 5, 0, 0, 6);
    CHECK_EQ(g_lastMark, kNE * 100 + 5); CHECK_EQ(g_lastArg, 5006);
    Reset(); SlotAt<void (*)(const void*, void*, uint8_t)>(n, kS6)(n, nullptr, 3);
    CHECK_EQ(g_lastMark, kNE * 100 + 6);
    // 7..11 are Element's, shared with TextElement.
    for (int s = 7; s <= 11; ++s) CHECK(VTableOf(n)[Retail(s)] == VTableOf(&te)[Retail(s)]);
}

// ===========================================================================
// PART C — ABI of the class-valued arguments / returns
// ===========================================================================
UNIT_TEST(ui_c3_slot8_createinstance_carries_the_hidden_return_slot) {
    LabelDescriptionProbe probe;
    void* raw = static_cast<void*>(&probe);
    const void* const* vtBefore = VTableOf(raw);

    void* slot = nullptr;
    const void* dialog = reinterpret_cast<const void*>(static_cast<uintptr_t>(0xD1A1));
    Reset();
    void** back = SlotAt<void** (*)(const void*, void**, const void*)>(raw, kS8)(raw, &slot, dialog);
    CHECK_EQ(g_lastMark, kLD * 100 + 8);
    CHECK(g_lastSelf == raw);
    CHECK_EQ(g_lastArg, 0xD1A1);
    CHECK(back == &slot);
    CHECK(slot == kRetSentinel);
    CHECK(VTableOf(raw) == vtBefore);
}

UNIT_TEST(ui_c3_editelement_update_receives_a_pointer_to_the_handle) {
    FactoryScope scope;
    EditElementProbe probe{};
    void* raw = static_cast<void*>(&probe);

    // Retail order: RCX this, RDX time, R8 ui, R9 = &handle, [RSP+0x28] state.
    void* handle = reinterpret_cast<void*>(static_cast<uintptr_t>(0x4000));
    UI::Element::UpdateState state;
    state.mouseX = 5;
    Reset();
    SlotAt<void (*)(void*, const void*, void*, void**, const void*)>(raw, kS7)(
        raw, nullptr, nullptr, &handle, &state);
    CHECK_EQ(g_lastMark, kEE * 100 + 7);
    CHECK_EQ(g_lastArg, 0x4000 + 5);
}

// ===========================================================================
// PART D — the QUALIFIED call on slot 12
// ===========================================================================
UNIT_TEST(ui_c3_qualified_finalizetext_does_not_redispatch) {
    FactoryScope scope;
    EditFinalizeProbe probe{};
    UI::EditElement* e = &probe;

    Reset(); e->FinalizeText();                        // virtual: the override
    CHECK_EQ(g_lastMark, kProbeFinalize);
    Reset(); e->UI::EditElement::FinalizeText();       // qualified: the base body
    CHECK_EQ(g_lastMark, kEE * 100 + 12);
}

// ===========================================================================
// ENDGAME B.3.9 UI-A (B39_RULES D1/D2, rule B9-16) — the CONCRETE classes.
// ===========================================================================
namespace {

// B3X W1: retail's flags-1 arm is `delete one` (T's own D0: the chain once,
// then T's class-scope operator delete, InternalMemFree == free here); the
// free is PROVEN on a dtor_check::Recording<T> stand-in. Retail's flags-3
// vector arm has no Itanium counterpart: `delete[]` of `new T[3]` runs three
// destructors, downward.
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
}  // namespace

UNIT_TEST(b39_uia_edit_number_slot0_by_pmf) {
    CHECK_DTOR_SLOT(UI::EditDescription);
    CHECK_SLOT(UI::EditDescription, Construct, 0x48/8);
    CHECK_DTOR_SLOT(UI::EditElement);
    CHECK_SLOT(UI::EditElement, FinalizeText, 0x60/8);
    CHECK_DTOR_SLOT(UI::NumberEditDescription);
    CHECK_SLOT(UI::NumberEditDescription, Construct, 0x48/8);
    CHECK_DTOR_SLOT(UI::NumberEditElement);
    CHECK_SLOT(UI::NumberEditElement, FinalizeText, 0x60/8);
    CHECK_DTOR_SLOT(UI::NumberDescription);
    CHECK_SLOT(UI::NumberDescription, CreateInstance, 0x40/8);
    CHECK_DTOR_SLOT(UI::NumberElement);
    CHECK_SLOT(UI::NumberElement, Draw, 0x30/8);
}

// The real ctors make retail's stores (each header cites the transcribed
// instructions); the untouched bytes stay 0xCD.
UNIT_TEST(b39_uia_edit_number_ctors_make_retails_stores) {
    FactoryScope scope;
    {
        alignas(8) uint8_t buf[0x88];
        std::memset(buf, 0xCD, sizeof buf);
        UI::EditDescription* d = ::new (buf) UI::EditDescription();
        CHECK_EQ(d->_id, -1);
        CHECK(d->_font == nullptr);
        CHECK(d->_initialText._data == nullptr);
        CHECK_EQ(d->_maxLength, 0);
        CHECK_EQ((int)d->_password, 0);
        CHECK_EQ((int)buf[0x7d], 0xCD);
        CHECK(d->_allowableCharacters._data == nullptr);
        g_b39DtorCount = 0;
        d->~EditDescription();
        CHECK_EQ(g_b39DtorCount, 1);
    }
    {
        alignas(8) uint8_t buf[0x88];
        std::memset(buf, 0xCD, sizeof buf);
        UI::NumberEditDescription* d = ::new (buf) UI::NumberEditDescription();
        CHECK(d->_font == nullptr);
        CHECK_EQ((uint32_t)d->_initialValue, 0xCDCDCDCDu);   // NEVER written by the ctor
        CHECK_EQ(d->_maxLength, 6);
        CHECK_EQ(d->_minValue, -32767);
        CHECK_EQ(d->_maxValue, 0x7fff);
        CHECK_EQ((int)d->_allowNegative, 1);
        d->~NumberEditDescription();
    }
    {
        alignas(8) uint8_t buf[0x88];
        std::memset(buf, 0xCD, sizeof buf);
        UI::NumberDescription* d = ::new (buf) UI::NumberDescription();
        CHECK(d->_font == nullptr);
        CHECK_EQ(d->_numberType, 0u);
        CHECK_EQ(d->_ivalue, 0);
        CHECK(d->_fvalue == 0.0f);
        CHECK_EQ((int)d->_showSign, 0);
        d->~NumberDescription();
    }
    {
        alignas(8) uint8_t buf[0x100];
        std::memset(buf, 0xCD, sizeof buf);
        UI::NumberEditElement* e = ::new (buf) UI::NumberEditElement();
        CHECK_EQ(e->_alignment, 3);                 // Element() ran
        CHECK(e->_font == nullptr);
        CHECK(e->_text == nullptr);
        CHECK_EQ(e->_maxLength, 0);
        CHECK_EQ((int)e->_password, 0);
        CHECK_EQ((int)e->_padD5[0], 0xCD);
        CHECK(e->_filter == nullptr);
        CHECK_EQ((int)e->_active, 0);
        CHECK_EQ(e->_caret, 0);
        CHECK_EQ((int)e->_blinkOn, 0);
        CHECK_EQ(e->_blinkAccumulator, 0);
        CHECK_EQ(e->_min, -32767);
        CHECK_EQ(e->_max, 0x8000);
        NumberEditElementProbe probe{};
        CHECK(VTableOf(e) != VTableOf(&probe));
        g_b39DtorCount = 0;
        e->~NumberEditElement();                    // retail slot 0, flags 0
        CHECK_EQ(g_b39DtorCount, 2);                // ~NumberEditElement, then ~EditElement
    }
    {
        alignas(8) uint8_t buf[0xe8];
        std::memset(buf, 0xCD, sizeof buf);
        UI::g_numberCacheTextCalls = 0;
        UI::NumberElement* e = ::new (buf) UI::NumberElement();
        CHECK_EQ(UI::g_numberCacheTextCalls, 1);        // retail's ctor ends in CacheText
        CHECK(e->_font == nullptr);
        CHECK_EQ(e->_mode, 0);
        CHECK_EQ(e->_padCC, 0xCDCDCDCDu);
        CHECK(e->_cachedText._data == nullptr);
        CHECK_EQ(e->_intValue, 0);
        CHECK(e->_floatValue == 0.0f);
        CHECK_EQ((int)e->_showSign, 0);
        e->~NumberElement();
    }
}

UNIT_TEST(b39_uia_edit_number_deletingdtor_both_arms) {
    FactoryScope scope;
    B39ExpectBothArms<UI::EditDescription>(0x88, [](void* p) { return ::new (p) UI::EditDescription(); });
    B39ExpectBothArms<UI::NumberEditDescription>(0x88, [](void* p) { return ::new (p) UI::NumberEditDescription(); });
    B39ExpectBothArms<UI::NumberDescription>(0x88, [](void* p) { return ::new (p) UI::NumberDescription(); });
    B39ExpectBothArms<UI::EditElement>(0xf8, [](void* p) { return ::new (p) UI::EditElement(); });
    B39ExpectBothArms<UI::NumberElement>(0xe8, [](void* p) { return ::new (p) UI::NumberElement(); });
}

UNIT_TEST(b39_uia_text_label_slot0_by_pmf) {
    CHECK_DTOR_SLOT(UI::TextDescription);   // 0x180041e30
    CHECK_SLOT(UI::TextDescription, Construct, 0x48/8);
    CHECK_DTOR_SLOT(UI::LabelDescription);   // 0x180042730
    CHECK_SLOT(UI::LabelDescription, Construct, 0x48/8);
    CHECK_DTOR_SLOT(UI::TextElement);   // 0x1800422f0
    CHECK_SLOT(UI::TextElement, Draw, 0x30/8);
    CHECK_DTOR_SLOT(UI::LabelElement);   // 0x180042bb0
    CHECK_SLOT(UI::LabelElement, SetColor, 0x50/8);
}

UNIT_TEST(b39_uia_text_label_ctors_make_retails_stores) {
    FactoryScope scope;
    {
        alignas(8) uint8_t buf[0x90];
        std::memset(buf, 0xCD, sizeof buf);
        UI::TextDescription* d = ::new (buf) UI::TextDescription();
        CHECK_EQ(d->_id, -1);                         // ElementDescription() ran
        CHECK(d->_font == nullptr);
        CHECK(d->_stringTable2 == nullptr);
        CHECK(d->_name._data == nullptr);
        CHECK_EQ(d->_justify, 0u);
        CHECK_EQ((int)d->_dropShadow, 0);
        CHECK_EQ((int)d->_wordWrap, 0);
        CHECK_EQ((int)d->_pad86[0], 0xCD);            // NOT written
        CHECK_EQ((int)d->_pad86[1], 0xCD);
        CHECK_EQ(d->_wordWrapSize, 0xc8);
        CHECK_EQ(d->_maxWidth, -1);
        TextDescriptionProbe probe;
        CHECK(VTableOf(d) != VTableOf(&probe));
        g_b39DtorCount = 0;
        d->~TextDescription();
        CHECK_EQ(g_b39DtorCount, 1);
    }
    {
        alignas(8) uint8_t buf[0xb8];
        std::memset(buf, 0xCD, sizeof buf);
        UI::LabelDescription* d = ::new (buf) UI::LabelDescription();
        CHECK_EQ(d->_wordWrapSize, 0xc8);                      // TextDescription() ran
        CHECK_EQ(d->_textAlignment, 3u);
        CHECK_EQ(d->_labelPosition, 1u);
        CHECK_EQ(d->_textWidth, 0);
        CHECK_EQ(d->_spacing, 2);
        CHECK_EQ(d->_imageWidth, -1);
        CHECK_EQ(d->_imageHeight, -1);
        CHECK(d->_spriteSheet == nullptr);
        CHECK(d->_spriteName._data == nullptr);
        g_b39DtorCount = 0;
        d->~LabelDescription();                       // retail slot 0, flags 0
        CHECK_EQ(g_b39DtorCount, 2);                  // ~LabelDescription, ~TextDescription
    }
    {
        alignas(8) uint8_t buf[0x100];
        std::memset(buf, 0xCD, sizeof buf);
        UI::g_textCacheTextCalls = 0;
        UI::TextElement* e = ::new (buf) UI::TextElement();
        CHECK_EQ(UI::g_textCacheTextCalls, 1);            // retail's ctor ends in CacheText
        CHECK_EQ(e->_alignment, 3);                   // Element() ran
        CHECK(e->_font == nullptr);
        CHECK(e->_params._data == nullptr);
        CHECK_EQ(e->_params._size, 0);
        CHECK_EQ(e->_params._capacity, 0);
        CHECK(e->_text.table == nullptr);
        CHECK_EQ(e->_text.index, -1);
        CHECK_EQ((int)buf[0xe4], 0xCD);               // NOT written
        CHECK_EQ((int)e->_drawShadow, 0);
        CHECK_EQ((int)e->_wrap, 0);
        CHECK_EQ((int)e->_padEA[0], 0xCD);            // NOT written
        CHECK_EQ(e->_wordWrapSize, 100);
        CHECK_EQ(e->_maxWidth, -1);
        CHECK_EQ(e->_justify, 0);
        CHECK(e->_cachedText._data == nullptr);
        g_b39DtorCount = 0;
        e->~TextElement();
        CHECK_EQ(g_b39DtorCount, 1);
    }
    {
        alignas(8) uint8_t buf[0x130];
        std::memset(buf, 0xCD, sizeof buf);
        UI::g_textCacheTextCalls = 0;
        UI::LabelElement* e = ::new (buf) UI::LabelElement();
        CHECK_EQ(UI::g_textCacheTextCalls, 1);            // through TextElement()
        CHECK_EQ(e->_wordWrapSize, 100);
        CHECK(e->_sprite.spriteSheet == nullptr);
        CHECK_EQ(e->_sprite.spriteIndex, -1);
        CHECK_EQ((uint32_t)e->_sprite.pad0C, 0xCDCDCDCDu);   // NOT written
        CHECK_EQ(e->_align, 3);
        CHECK_EQ(e->_mode, 1u);
        CHECK_EQ(e->_minTextWidth, 0);
        CHECK_EQ(e->_gap, 2);
        CHECK_EQ(e->_iconWidth, -1);
        CHECK_EQ(e->_iconHeight, -1);
        CHECK_EQ(e->_iconColor, 0xffffffffu);
        CHECK_EQ(e->_textWidth, 0);
        LabelElementProbe probe{};
        CHECK(VTableOf(e) != VTableOf(&probe));
        g_b39DtorCount = 0;
        e->~LabelElement();                           // retail slot 0, flags 0
        CHECK_EQ(g_b39DtorCount, 2);                  // ~LabelElement, ~TextElement

    }
}

// Retired vector arms: TextDescription 0x180041e30, TextElement 0x1800422f0.
UNIT_TEST(b39_uia_text_label_deletingdtor_both_arms) {
    FactoryScope scope;
    B39ExpectBothArms<UI::TextDescription>(0x90, [](void* p) { return ::new (p) UI::TextDescription(); });
    B39ExpectBothArms<UI::TextElement>(0x100, [](void* p) { return ::new (p) UI::TextElement(); });
}
