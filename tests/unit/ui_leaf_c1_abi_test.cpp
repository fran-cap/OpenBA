// ============================================================================
// tests/unit/ui_leaf_c1_abi_test.cpp — ENDGAME B.3 step 6, block C1
//   the ten Button / Check / Combo / Border / ClipRegion leaf classes
//
//   UI::ButtonDescription      : ElementDescription  0xa0  10 slots
//   UI::CheckDescription       : ButtonDescription   0xa8  10 slots
//   UI::ButtonElement          : Element             0xd8  12 slots
//   UI::CheckElement           : ButtonElement       0xe0  12 slots
//   UI::ComboDescription       : ButtonDescription   0xb8  10 slots
//   UI::ComboElement           : ButtonElement       0x128 12 slots
//   UI::BorderDescription      : ElementDescription  0xb8  10 slots
//   UI::BorderElement          : Element             0x150 12 slots
//   UI::ClipRegionDescription  : ElementDescription  0x78  10 slots
//   UI::ClipRegionElement      : Element             0xd8  12 slots
//
// Built to the contract of ui_element_abi_test.cpp (block A); only what
// differs is spelled out.
//
// PART A — LAYOUT: sizeof / alignof / every offset the headers claim, the
//   class-vs-POD agreement for the Descriptions (DescriptionsBatch1.h), the
//   parent edges `_ParentTypeName` proves, abstract (B3X W1: a public virtual destructor).
// PART B — SLOT ORDER: probe subclasses of the REAL classes; each own slot is
//   raw-called by byte offset and the marker that runs names the member; each
//   INHERITED slot is compared, pointer for pointer, with the parent's entry
//   (the `inherited-from` markers of B36_SLOTS.md section 1).
// PART C — THE RETSLOT SLOT: Description slot 8 `CreateInstance` raw-called the
//   retail way `(this, ret, dialog)` (RULE B6-2).
//
// `UI::Element` / `UI::ElementDescription`'s own out-of-line members are the
// MARKERS ui_element_abi_test.cpp defines; this file defines the ten leaves'
// members as its own markers (src/engine/ui does not link into a host test).
// ============================================================================
#include "unit.h"
#include "slot_check.h"
#include "dtor_check.h"

#include "ui/Element.h"
#include "ui/ElementDescription.h"
#include "ui/ButtonDescription.h"
#include "ui/ButtonElement.h"
#include "ui/CheckDescription.h"
#include "ui/CheckElement.h"
#include "ui/ComboDescription.h"
#include "ui/ComboElement.h"
#include "ui/BorderDescription.h"
#include "ui/BorderElement.h"
#include "ui/ClipRegionDescription.h"
#include "ui/ClipRegionElement.h"
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

// ---------------------------------------------------------------------------
// Observation channel: WHICH class's member ran, and which slot it stands for.
// ---------------------------------------------------------------------------
enum Who : int { kNone = 0, kButtonD, kCheckD, kButtonE, kCheckE, kComboD, kComboE,
                 kBorderD, kBorderE, kClipD, kClipE, kElementBase };

const void* g_self = nullptr;
int         g_who  = kNone;
int         g_slot = -1;
long long   g_arg  = 0;
int         g_trail[8];   // the order of marker entries inside one call
int         g_trailN = 0;

void Note(const void* self, int who, int slot, long long arg = 0) {
    g_self = self; g_who = who; g_slot = slot; g_arg = arg;
    if (g_trailN < 8) g_trail[g_trailN++] = who * 100 + slot;
}
void Reset() { g_self = nullptr; g_who = kNone; g_slot = -1; g_arg = 0; g_trailN = 0; }

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

// Byte offsets of the two table shapes (B36_SLOTS.md section 1).
enum : int {
    kE0 = 0x00, kE1_TypeName = 0x08, kE2_Type = 0x10, kE3_Persist = 0x18,
    kE4_ComputeSize = 0x20, kE5_ComputeLayout = 0x28, kE6_Draw = 0x30,
    kE7_Update = 0x38, kE8_Enable = 0x40, kE9_Contains = 0x48,
    kE10_SetColor = 0x50, kE11_GetBorder = 0x58,
};
enum : int {
    kD0 = 0x00, kD1_TypeName = 0x08, kD2_Type = 0x10, kD3_GetFileVersion = 0x18,
    kD4_Compile = 0x20, kD5_Serialize = 0x28, kD6_Reconstruct = 0x30,
    kD7_Destruct = 0x38, kD8_CreateInstance = 0x40, kD9_Construct = 0x48,
};

// ---------------------------------------------------------------------------
// Probes. Each is a final subclass with its own table and its own implicit
// destructor (B3X W1: retail slot 0 is the destructor pair), so every other
// entry in its table is the leaf's (or an ancestor's) member.
// ---------------------------------------------------------------------------
struct BareElementProbe final : UI::Element {          // the parent's entries
    void ComputeSize() override {}
};
struct ButtonElementProbe final : UI::ButtonElement {};
struct CheckElementProbe final : UI::CheckElement {};
struct BorderElementProbe final : UI::BorderElement {};
struct BorderDescriptionProbe final : UI::BorderDescription {};
struct ClipRegionElementProbe final : UI::ClipRegionElement {};
struct ClipRegionDescriptionProbe final : UI::ClipRegionDescription {};
struct ComboElementProbe final : UI::ComboElement {};
struct ComboDescriptionProbe final : UI::ComboDescription {};
struct ButtonDescriptionProbe final : UI::ButtonDescription {};
struct CheckDescriptionProbe final : UI::CheckDescription {};

// Plain `Object::External` leaf: its slot 4 is `External::Compile`.
struct BareExternalProbe final : Object::External {
    int32_t GetFileVersion() const override { return 0; }
    bool Serialize(IO::Stream&) override { return true; }
    Object::External* Reconstruct() override { return this; }
    void Destruct() override {}
};

}  // namespace

// ENDGAME B.3.9 UI-A: the destructor markers of the CONCRETE classes RECORD
// each call, so the destructors (B3X W1: D1, D0 and delete[], the retired
// slot-0 arms) can be seen running ~X once per element and in retail's
// DESCENDING order.
namespace {
int         g_b39DtorCount = 0;
const void* g_b39DtorOrder[8] = {};
void B39NoteDtor(const void* self) {
    if (g_b39DtorCount < 8) g_b39DtorOrder[g_b39DtorCount] = self;
    ++g_b39DtorCount;
}
}  // namespace

// ===========================================================================
// The leaves' out-of-line members, AS MARKERS.
// ===========================================================================
namespace UI {

// ---- ButtonElement ----
void ButtonElement::ComputeSize() { Note(this, kButtonE, 4); }
void ButtonElement::ComputeLayout(int32_t x, int32_t, int32_t, int32_t h) {
    Note(this, kButtonE, 5, (long long)x * 1000 + h);
}
void ButtonElement::Draw(SpriteContext&, uint8_t alpha) const { Note(this, kButtonE, 6, alpha); }
void ButtonElement::Update(const System::Time&, UserInterface*,
                           Object::InternalPtr<DialogInstance>* dialog, const UpdateState& state) {
    Note(this, kButtonE, 7,
         static_cast<long long>(reinterpret_cast<intptr_t>(dialog->Get())) + state.mouseX);
}
void ButtonElement::Enable(bool enable) { Note(this, kButtonE, 8, enable ? 1 : 0); }
ButtonElement::~ButtonElement() { B39NoteDtor(this); }

// ---- CheckElement — Update calls its PARENT's body QUALIFIED, as the real one does.
void CheckElement::ComputeSize() { Note(this, kCheckE, 4); }
void CheckElement::ComputeLayout(int32_t x, int32_t, int32_t, int32_t h) {
    Note(this, kCheckE, 5, (long long)x * 1000 + h);
}
void CheckElement::Draw(SpriteContext&, uint8_t alpha) const { Note(this, kCheckE, 6, alpha); }
void CheckElement::Update(const System::Time& time, UserInterface* ui,
                          Object::InternalPtr<DialogInstance>* dialog, const UpdateState& state) {
    Note(this, kCheckE, 7);
    Object::InternalPtr<DialogInstance> copy = *dialog;
    ButtonElement::Update(time, ui, &copy, state);
}
CheckElement::~CheckElement() { B39NoteDtor(this); }

// ---- ButtonDescription ----
int32_t ButtonDescription::GetFileVersion() const { Note(this, kButtonD, 3); return 0x104; }
bool ButtonDescription::Serialize(IO::Stream& stream) {
    Note(this, kButtonD, 5, reinterpret_cast<intptr_t>(&stream)); return true;
}
Object::External* ButtonDescription::Reconstruct() { Note(this, kButtonD, 6); return this; }
void ButtonDescription::Destruct() { Note(this, kButtonD, 7); }
Object::InternalPtr<Element>* ButtonDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* dialog) const {
    Note(this, kButtonD, 8, reinterpret_cast<intptr_t>(dialog));
    new (static_cast<void*>(ret)) Object::InternalPtr<Element>(
        reinterpret_cast<Element*>(static_cast<uintptr_t>(0xB1B1B1B1)));
    return ret;
}
Object::External* ButtonDescription::Construct() { Note(this, kButtonD, 9); return this; }
ButtonDescription::~ButtonDescription() { B39NoteDtor(this); }

// ---- CheckDescription — Serialize calls ButtonDescription's QUALIFIED.
int32_t CheckDescription::GetFileVersion() const { Note(this, kCheckD, 3); return 0x1104; }
bool CheckDescription::Serialize(IO::Stream& stream) {
    Note(this, kCheckD, 5);
    return ButtonDescription::Serialize(stream);
}
Object::External* CheckDescription::Reconstruct() { Note(this, kCheckD, 6); return this; }
void CheckDescription::Destruct() { Note(this, kCheckD, 7); }
Object::InternalPtr<Element>* CheckDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* dialog) const {
    Note(this, kCheckD, 8, reinterpret_cast<intptr_t>(dialog));
    new (static_cast<void*>(ret)) Object::InternalPtr<Element>(
        reinterpret_cast<Element*>(static_cast<uintptr_t>(0xC1C1C1C1)));
    return ret;
}
Object::External* CheckDescription::Construct() { Note(this, kCheckD, 9); return this; }
CheckDescription::~CheckDescription() { B39NoteDtor(this); }

// ---- ComboElement: overrides slots 1, 2 and 7 only.
void ComboElement::Update(const System::Time& time, UserInterface* ui,
                          Object::InternalPtr<DialogInstance>* dialog, const UpdateState& state) {
    Note(this, kComboE, 7);
    Object::InternalPtr<DialogInstance> copy = *dialog;
    ButtonElement::Update(time, ui, &copy, state);
}
ComboElement::~ComboElement() { B39NoteDtor(this); }

// ---- ComboDescription — Serialize calls ButtonDescription's QUALIFIED.
int32_t ComboDescription::GetFileVersion() const { Note(this, kComboD, 3); return 0x2104; }
bool ComboDescription::Serialize(IO::Stream& stream) {
    Note(this, kComboD, 5);
    return ButtonDescription::Serialize(stream);
}
Object::External* ComboDescription::Reconstruct() { Note(this, kComboD, 6); return this; }
void ComboDescription::Destruct() { Note(this, kComboD, 7); }
Object::InternalPtr<Element>* ComboDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* dialog) const {
    Note(this, kComboD, 8, reinterpret_cast<intptr_t>(dialog));
    new (static_cast<void*>(ret)) Object::InternalPtr<Element>(
        reinterpret_cast<Element*>(static_cast<uintptr_t>(0xC0C0C0C0)));
    return ret;
}
Object::External* ComboDescription::Construct() { Note(this, kComboD, 9); return this; }
ComboDescription::~ComboDescription() { B39NoteDtor(this); }

// ---- BorderElement: overrides 1, 2, 4, 5, 6.
void BorderElement::ComputeSize() { Note(this, kBorderE, 4); }
void BorderElement::ComputeLayout(int32_t x, int32_t, int32_t, int32_t h) {
    Note(this, kBorderE, 5, (long long)x * 1000 + h);
}
void BorderElement::Draw(SpriteContext&, uint8_t alpha) const { Note(this, kBorderE, 6, alpha); }
BorderElement::~BorderElement() { B39NoteDtor(this); }

// ---- ClipRegionElement: overrides 1, 2, 4, 5, 6, 7 and 9. Contains chains
// to Element::Contains QUALIFIED, as the real body does.
void ClipRegionElement::ComputeSize() { Note(this, kClipE, 4); }
void ClipRegionElement::ComputeLayout(int32_t x, int32_t, int32_t, int32_t h) {
    Note(this, kClipE, 5, (long long)x * 1000 + h);
}
void ClipRegionElement::Draw(SpriteContext&, uint8_t alpha) const { Note(this, kClipE, 6, alpha); }
void ClipRegionElement::Update(const System::Time&, UserInterface*,
                               Object::InternalPtr<DialogInstance>*, const UpdateState&) {
    Note(this, kClipE, 7);
}
bool ClipRegionElement::Contains(int32_t x, int32_t y, UserInterface* ui) const {
    Note(this, kClipE, 9);
    return Element::Contains(x, y, ui);
}
ClipRegionElement::~ClipRegionElement() { B39NoteDtor(this); }

// ---- BorderDescription / ClipRegionDescription: direct ElementDescription leaves.
int32_t BorderDescription::GetFileVersion() const { Note(this, kBorderD, 3); return 0x104; }
bool BorderDescription::Serialize(IO::Stream& stream) {
    Note(this, kBorderD, 5, reinterpret_cast<intptr_t>(&stream)); return true;
}
Object::External* BorderDescription::Reconstruct() { Note(this, kBorderD, 6); return this; }
void BorderDescription::Destruct() { Note(this, kBorderD, 7); }
Object::InternalPtr<Element>* BorderDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* dialog) const {
    Note(this, kBorderD, 8, reinterpret_cast<intptr_t>(dialog));
    new (static_cast<void*>(ret)) Object::InternalPtr<Element>(
        reinterpret_cast<Element*>(static_cast<uintptr_t>(0xBDBDBDBD)));
    return ret;
}
Object::External* BorderDescription::Construct() { Note(this, kBorderD, 9); return this; }
BorderDescription::~BorderDescription() { B39NoteDtor(this); }

int32_t ClipRegionDescription::GetFileVersion() const { Note(this, kClipD, 3); return 0x104; }
bool ClipRegionDescription::Serialize(IO::Stream& stream) {
    Note(this, kClipD, 5, reinterpret_cast<intptr_t>(&stream)); return true;
}
Object::External* ClipRegionDescription::Reconstruct() { Note(this, kClipD, 6); return this; }
void ClipRegionDescription::Destruct() { Note(this, kClipD, 7); }
Object::InternalPtr<Element>* ClipRegionDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* dialog) const {
    Note(this, kClipD, 8, reinterpret_cast<intptr_t>(dialog));
    new (static_cast<void*>(ret)) Object::InternalPtr<Element>(
        reinterpret_cast<Element*>(static_cast<uintptr_t>(0xC1C2C1C2)));
    return ret;
}
Object::External* ClipRegionDescription::Construct() { Note(this, kClipD, 9); return this; }
ClipRegionDescription::~ClipRegionDescription() { B39NoteDtor(this); }

}  // namespace UI

// ENDGAME B.3.9 TI-UI: slots 1/2 are DECLARE_TYPE's inline bodies over each
// class's own record, so the host defines the records (the literals are the
// ones src/engine/ui/*.cpp passes) and the factory pair they point at; the
// factories are never called here.
#define C1_TEST_FACTORIES(CLS)                                                  \
    ::Object::Base* UI::CLS::CreateNewObject() { return nullptr; }              \
    ::Object::Base* UI::CLS::CreateInPlaceObject(void*) { return nullptr; }
C1_TEST_FACTORIES(ButtonElement)
DEFINE_TYPE(UI::ButtonElement, "Element", nullptr)
C1_TEST_FACTORIES(CheckElement)
DEFINE_TYPE(UI::CheckElement, "ButtonElement", nullptr)
C1_TEST_FACTORIES(ButtonDescription)
DEFINE_TYPE(UI::ButtonDescription, "ElementDescription", nullptr)
C1_TEST_FACTORIES(CheckDescription)
DEFINE_TYPE(UI::CheckDescription, "ButtonDescription", nullptr)
C1_TEST_FACTORIES(ComboElement)
DEFINE_TYPE(UI::ComboElement, "ButtonElement", nullptr)
C1_TEST_FACTORIES(ComboDescription)
DEFINE_TYPE(UI::ComboDescription, "ButtonDescription", nullptr)
C1_TEST_FACTORIES(BorderElement)
DEFINE_TYPE(UI::BorderElement, "Element", nullptr)
C1_TEST_FACTORIES(ClipRegionElement)
DEFINE_TYPE(UI::ClipRegionElement, "Element", nullptr)
C1_TEST_FACTORIES(BorderDescription)
DEFINE_TYPE(UI::BorderDescription, "ElementDescription", nullptr)
C1_TEST_FACTORIES(ClipRegionDescription)
DEFINE_TYPE(UI::ClipRegionDescription, "ElementDescription", nullptr)
#undef C1_TEST_FACTORIES

namespace {

// Slots 1/2 (TI-UI): the record of the class `who` names.
const ::Object::BaseTypeInfo* WhoRecord(int who) {
    switch (who) {
    case kButtonD: return &UI::ButtonDescription::GetTypeInfo();
    case kCheckD: return &UI::CheckDescription::GetTypeInfo();
    case kButtonE: return &UI::ButtonElement::GetTypeInfo();
    case kCheckE: return &UI::CheckElement::GetTypeInfo();
    case kComboD: return &UI::ComboDescription::GetTypeInfo();
    case kComboE: return &UI::ComboElement::GetTypeInfo();
    case kBorderD: return &UI::BorderDescription::GetTypeInfo();
    case kBorderE: return &UI::BorderElement::GetTypeInfo();
    case kClipD: return &UI::ClipRegionDescription::GetTypeInfo();
    case kClipE: return &UI::ClipRegionElement::GetTypeInfo();
    default: return nullptr;
    }
}

// Raw-call every OWN slot of an Element-shaped probe and check the marker.
void ExpectElementSlot(void* raw, int off, int who) {
    const int slot = off / 8;
    Reset();
    switch (off) {
    case kE1_TypeName:   // TI-UI: the class's own record, by what it returns
        CHECK(SlotAt<const char* (*)(const void*)>(raw, off)(raw) == WhoRecord(who)->name);
        CHECK(std::strcmp(WhoRecord(who)->name, "") != 0); CHECK_EQ(g_who, (int)kNone); return;
    case kE2_Type:
        CHECK_EQ(SlotAt<uint32_t (*)(const void*)>(raw, off)(raw), WhoRecord(who)->typeId);
        CHECK_EQ(g_who, (int)kNone); return;
    case kE4_ComputeSize: SlotAt<void (*)(void*)>(raw, off)(raw); break;
    case kE5_ComputeLayout:
        SlotAt<void (*)(void*, int, int, int, int)>(raw, off)(raw, 7, 0, 0, 9);
        CHECK_EQ(g_arg, 7009); break;
    case kE6_Draw:
        SlotAt<void (*)(const void*, void*, uint8_t)>(raw, off)(raw, nullptr, 200);
        CHECK_EQ(g_arg, 200); break;
    case kE8_Enable:
        SlotAt<void (*)(void*, bool)>(raw, off)(raw, true);
        CHECK_EQ(g_arg, 1); break;
    default: CHECK(false); break;
    }
    CHECK_EQ(g_who, who);
    CHECK_EQ(g_slot, slot);
    CHECK(g_self == raw);
}

// Raw-call every OWN slot of a Description-shaped probe and check the marker.
void ExpectDescriptionSlot(void* raw, int off, int who) {
    const int slot = off / 8;
    Reset();
    switch (off) {
    case kD1_TypeName:   // TI-UI: the class's own record, by what it returns
        CHECK(SlotAt<const char* (*)(const void*)>(raw, off)(raw) == WhoRecord(who)->name);
        CHECK_EQ(g_who, (int)kNone); return;
    case kD2_Type:
        CHECK_EQ(SlotAt<uint32_t (*)(const void*)>(raw, off)(raw), WhoRecord(who)->typeId);
        CHECK_EQ(g_who, (int)kNone); return;
    case kD3_GetFileVersion: SlotAt<int32_t (*)(const void*)>(raw, off)(raw); break;
    case kD5_Serialize:
        SlotAt<bool (*)(void*, void*)>(raw, off)(raw, (void*)0x55); break;
    case kD6_Reconstruct: SlotAt<void* (*)(void*)>(raw, off)(raw); break;
    case kD7_Destruct:    SlotAt<void (*)(void*)>(raw, off)(raw); break;
    case kD9_Construct:   SlotAt<void* (*)(void*)>(raw, off)(raw); break;
    default: CHECK(false); break;
    }
    // Serialize on a Check/ComboDescription runs its own marker, then the base's.
    if (off == kD5_Serialize && (who == kCheckD || who == kComboD)) {
        CHECK_EQ(g_trailN, 2);
        CHECK_EQ(g_trail[0], who * 100 + 5);
        CHECK_EQ(g_trail[1], kButtonD * 100 + 5);
    } else {
        CHECK_EQ(g_who, who);
        CHECK_EQ(g_slot, slot);
    }
    CHECK(g_self == raw);
}

}  // namespace

// ===========================================================================
// PART A — LAYOUT
// ===========================================================================
UNIT_TEST(ui_c1_button_check_sizes_and_parents) {
    // `MOV EDX,imm` in each class's own slot-0 vector deleting destructor.
    CHECK_EQ(alignof(UI::ButtonElement), (size_t)8);
    CHECK_EQ(alignof(UI::CheckElement), (size_t)8);
    CHECK_EQ(alignof(UI::ButtonDescription), (size_t)8);
    CHECK_EQ(alignof(UI::CheckDescription), (size_t)8);

    // The parent edges `_ParentTypeName` proves (B36_SLOTS.md section 3).
    CHECK((std::is_base_of<UI::Element, UI::ButtonElement>::value));
    CHECK((std::is_base_of<UI::ButtonElement, UI::CheckElement>::value));
    CHECK((std::is_base_of<UI::ElementDescription, UI::ButtonDescription>::value));
    CHECK((std::is_base_of<UI::ButtonDescription, UI::CheckDescription>::value));

    CHECK(!std::is_abstract<UI::ButtonElement>::value);      // B.3.9 UI-A: concrete
    CHECK(std::is_destructible<UI::ButtonElement>::value);  // B3X W1: public virtual dtor
    CHECK(!std::is_abstract<UI::CheckElement>::value);      // B.3.9 UI-A: concrete
    CHECK(std::is_destructible<UI::CheckElement>::value);  // B3X W1: public virtual dtor
    CHECK(!std::is_abstract<UI::ButtonDescription>::value);      // B.3.9 UI-A: concrete
    CHECK(std::is_destructible<UI::ButtonDescription>::value);  // B3X W1: public virtual dtor
    CHECK(!std::is_abstract<UI::CheckDescription>::value);      // B.3.9 UI-A: concrete
    CHECK(std::is_destructible<UI::CheckDescription>::value);  // B3X W1: public virtual dtor
    CHECK(std::has_virtual_destructor<UI::ButtonElement>::value);
    CHECK(std::has_virtual_destructor<UI::CheckElement>::value);
    CHECK(std::has_virtual_destructor<UI::ButtonDescription>::value);
    CHECK(std::has_virtual_destructor<UI::CheckDescription>::value);
}

UNIT_TEST(ui_c1_button_check_offsets) {
    // Class and POD are one layout (DescriptionsBatch1.h is not edited).
    CHECK_EQ(sizeof(UI::ButtonDescription), sizeof(RtUI::ButtonDescription));
    CHECK_EQ(sizeof(UI::CheckDescription),  sizeof(RtUI::CheckDescription));
    CHECK_EQ(offsetof(UI::CheckDescription, _checkDesc), offsetof(RtUI::CheckDescription, _checkDesc));
    // The name-hiding the header documents: the five hide the base's one.
}

// ===========================================================================
// PART B — SLOT ORDER
// ===========================================================================
UNIT_TEST(ui_c1_buttonelement_slot_order_is_retails) {
    FactoryScope scope;
    ButtonElementProbe probe{};
    BareElementProbe parent{};
    void* raw = static_cast<void*>(&probe);
    const void* const* vt = VTableOf(raw);
    const void* const* pv = VTableOf(static_cast<void*>(&parent));

    for (int off : { kE1_TypeName, kE2_Type, kE4_ComputeSize, kE5_ComputeLayout,
                     kE6_Draw, kE8_Enable })
        ExpectElementSlot(raw, off, kButtonE);
    // Slot 7 through the retail pointer-to-handle shape.
    void* handle = reinterpret_cast<void*>(static_cast<uintptr_t>(0x7000));
    UI::Element::UpdateState state;
    state.mouseX = 5;
    Reset();
    SlotAt<void (*)(void*, const void*, void*, void**, const void*)>(raw, kE7_Update)(
        raw, nullptr, nullptr, &handle, &state);
    CHECK_EQ(g_who, (int)kButtonE); CHECK_EQ(g_slot, 7); CHECK_EQ(g_arg, 0x7000 + 5);

    // INHERITED from UI::Element: 3 Persist, 9 Contains, 10 SetColor, 11 GetBorder.
    for (int off : { kE3_Persist, kE9_Contains, kE10_SetColor, kE11_GetBorder })
        CHECK(vt[Retail(off / 8)] == pv[Retail(off / 8)]);
    // ... and the six own ones are NOT the parent's.
    for (int off : { kE1_TypeName, kE2_Type, kE4_ComputeSize, kE5_ComputeLayout,
                     kE6_Draw, kE7_Update, kE8_Enable })
        CHECK(vt[Retail(off / 8)] != pv[Retail(off / 8)]);

    CHECK(static_cast<void*>(static_cast<UI::Element*>(&probe)) == raw);
}

UNIT_TEST(ui_c1_checkelement_slot_order_is_retails) {
    FactoryScope scope;
    CheckElementProbe probe{};
    ButtonElementProbe button{};
    BareElementProbe element{};
    void* raw = static_cast<void*>(&probe);
    const void* const* vt = VTableOf(raw);
    const void* const* bv = VTableOf(static_cast<void*>(&button));
    const void* const* ev = VTableOf(static_cast<void*>(&element));

    for (int off : { kE1_TypeName, kE2_Type, kE4_ComputeSize, kE5_ComputeLayout, kE6_Draw })
        ExpectElementSlot(raw, off, kCheckE);
    // Slot 8 is ButtonElement::Enable, INHERITED (0x180110cc0 in both tables).
    CHECK(vt[Retail(kE8_Enable / 8)] == bv[Retail(kE8_Enable / 8)]);
    ExpectElementSlot(raw, kE8_Enable, kButtonE);
    for (int off : { kE3_Persist, kE9_Contains, kE10_SetColor, kE11_GetBorder })
        CHECK(vt[Retail(off / 8)] == ev[Retail(off / 8)]);
    CHECK(vt[Retail(kE7_Update / 8)] != bv[Retail(kE7_Update / 8)]);
    CHECK(static_cast<void*>(static_cast<UI::ButtonElement*>(&probe)) == raw);
}

UNIT_TEST(ui_c1_buttondescription_slot_order_is_retails) {
    ButtonDescriptionProbe probe;
    BareExternalProbe bare;
    void* raw = static_cast<void*>(&probe);
    for (int off : { kD1_TypeName, kD2_Type, kD3_GetFileVersion, kD5_Serialize,
                     kD6_Reconstruct, kD7_Destruct, kD9_Construct })
        ExpectDescriptionSlot(raw, off, kButtonD);
    // Slot 4 is Object::External::Compile, inherited (RULE B6-1.5).
    CHECK(RetailEntry<Object::Base>(raw, kD4_Compile / 8) == RetailEntry<Object::Base>(static_cast<void*>(&bare), kD4_Compile / 8));
    // Ten distinct retail entries = eleven compiler entries (D1 != D0 too);
    // Construct (9) is BEHIND CreateInstance (8) — B6-1.1.
    const void* const* vt = VTableOf(raw);
    for (int i = 0; i < 11; ++i)
        for (int j = i + 1; j < 11; ++j)
            CHECK(vt[i] != vt[j]);
}

UNIT_TEST(ui_c1_checkdescription_slot_order_is_retails) {
    CheckDescriptionProbe probe;
    ButtonDescriptionProbe button;
    void* raw = static_cast<void*>(&probe);
    for (int off : { kD1_TypeName, kD2_Type, kD3_GetFileVersion, kD5_Serialize,
                     kD6_Reconstruct, kD7_Destruct, kD9_Construct })
        ExpectDescriptionSlot(raw, off, kCheckD);
    // Every slot but 4 (and 0, the probe's) is CheckDescription's OWN, not the
    // parent's: B36_SLOTS.md marks only slot 4 inherited.
    const void* const* vt = VTableOf(raw);
    const void* const* bv = VTableOf(static_cast<void*>(&button));
    CHECK(vt[Retail(kD4_Compile / 8)] == bv[Retail(kD4_Compile / 8)]);
    for (int i = 1; i < 10; ++i)
        if (i != kD4_Compile / 8) CHECK(vt[Retail(i)] != bv[Retail(i)]);
}

// ===========================================================================
// PART C — slot 8, raw-called the RETAIL way (RULE B6-2)
// ===========================================================================
namespace {
void ExpectCreateInstanceRetslot(void* raw, int who, uintptr_t made) {
    const void* const* vtBefore = VTableOf(raw);
    void* slot = nullptr;
    const void* dialog = reinterpret_cast<const void*>(static_cast<uintptr_t>(0xD1A1));
    Reset();
    void** back = SlotAt<void** (*)(const void*, void**, const void*)>(
        raw, kD8_CreateInstance)(raw, &slot, dialog);
    CHECK_EQ(g_who, who);
    CHECK_EQ(g_slot, 8);
    CHECK(g_self == raw);
    CHECK_EQ(g_arg, 0xD1A1);
    CHECK(back == &slot);
    CHECK(slot == reinterpret_cast<void*>(made));
    CHECK(VTableOf(raw) == vtBefore);
}
}  // namespace

UNIT_TEST(ui_c1_button_check_createinstance_carries_the_hidden_return_slot) {
    ButtonDescriptionProbe b;
    CheckDescriptionProbe c;
    ExpectCreateInstanceRetslot(static_cast<void*>(&b), kButtonD, 0xB1B1B1B1u);
    ExpectCreateInstanceRetslot(static_cast<void*>(&c), kCheckD, 0xC1C1C1C1u);
}

// ===========================================================================
// COMBO — layout, slot order, retslot
// ===========================================================================
UNIT_TEST(ui_c1_combo_layout) {
    CHECK_EQ(alignof(UI::ComboElement), (size_t)8);
    CHECK_EQ(alignof(UI::ComboDescription), (size_t)8);
    CHECK((std::is_base_of<UI::ButtonElement, UI::ComboElement>::value));
    CHECK((std::is_base_of<UI::ButtonDescription, UI::ComboDescription>::value));
    CHECK(!std::is_abstract<UI::ComboElement>::value);      // B.3.9 UI-A: concrete
    CHECK(std::is_destructible<UI::ComboElement>::value);  // B3X W1: public virtual dtor
    CHECK(!std::is_abstract<UI::ComboDescription>::value);      // B.3.9 UI-A: concrete
    CHECK(std::is_destructible<UI::ComboDescription>::value);  // B3X W1: public virtual dtor
    CHECK(std::has_virtual_destructor<UI::ComboElement>::value);
    CHECK(std::has_virtual_destructor<UI::ComboDescription>::value);

    CHECK_EQ(sizeof(UI::ComboDescription), sizeof(RtUI::ComboDescription));
    CHECK_EQ(offsetof(UI::ComboDescription, _stringTable),
             offsetof(RtUI::ComboDescription, _stringTable));
    // The name-hiding the header documents: the base's table is at +0x58.
}

UNIT_TEST(ui_c1_comboelement_slot_order_is_retails) {
    FactoryScope scope;
    ComboElementProbe probe{};
    ButtonElementProbe button{};
    BareElementProbe element{};
    void* raw = static_cast<void*>(&probe);
    const void* const* vt = VTableOf(raw);
    const void* const* bv = VTableOf(static_cast<void*>(&button));
    const void* const* ev = VTableOf(static_cast<void*>(&element));

    ExpectElementSlot(raw, kE1_TypeName, kComboE);
    ExpectElementSlot(raw, kE2_Type, kComboE);
    // 4/5/6/8 are ButtonElement's (B36_SLOTS.md: inherited-from UI::ButtonElement).
    for (int off : { kE4_ComputeSize, kE5_ComputeLayout, kE6_Draw, kE8_Enable }) {
        CHECK(vt[Retail(off / 8)] == bv[Retail(off / 8)]);
        ExpectElementSlot(raw, off, kButtonE);
    }
    for (int off : { kE3_Persist, kE9_Contains, kE10_SetColor, kE11_GetBorder })
        CHECK(vt[Retail(off / 8)] == ev[Retail(off / 8)]);
    CHECK(vt[Retail(kE7_Update / 8)] != bv[Retail(kE7_Update / 8)]);

    // Slot 7 is Combo's own and chains to ButtonElement's, qualified.
    void* handle = reinterpret_cast<void*>(static_cast<uintptr_t>(0x5000));
    UI::Element::UpdateState state;
    state.mouseX = 2;
    Reset();
    SlotAt<void (*)(void*, const void*, void*, void**, const void*)>(raw, kE7_Update)(
        raw, nullptr, nullptr, &handle, &state);
    CHECK_EQ(g_trailN, 2);
    CHECK_EQ(g_trail[0], kComboE * 100 + 7);
    CHECK_EQ(g_trail[1], kButtonE * 100 + 7);
    CHECK_EQ(g_arg, 0x5000 + 2);
}

UNIT_TEST(ui_c1_combodescription_slot_order_is_retails) {
    ComboDescriptionProbe probe;
    ButtonDescriptionProbe button;
    void* raw = static_cast<void*>(&probe);
    for (int off : { kD1_TypeName, kD2_Type, kD3_GetFileVersion, kD5_Serialize,
                     kD6_Reconstruct, kD7_Destruct, kD9_Construct })
        ExpectDescriptionSlot(raw, off, kComboD);
    const void* const* vt = VTableOf(raw);
    const void* const* bv = VTableOf(static_cast<void*>(&button));
    CHECK(vt[Retail(kD4_Compile / 8)] == bv[Retail(kD4_Compile / 8)]);
    for (int i = 1; i < 10; ++i)
        if (i != kD4_Compile / 8) CHECK(vt[Retail(i)] != bv[Retail(i)]);
    ExpectCreateInstanceRetslot(raw, kComboD, 0xC0C0C0C0u);
}

// ===========================================================================
// BORDER and CLIPREGION — layout, slot order, retslot, qualified Contains
// ===========================================================================
UNIT_TEST(ui_c1_border_clipregion_layout) {
    CHECK_EQ(alignof(UI::BorderElement), (size_t)8);
    CHECK_EQ(alignof(UI::BorderDescription), (size_t)8);
    CHECK_EQ(alignof(UI::ClipRegionElement), (size_t)8);
    CHECK_EQ(alignof(UI::ClipRegionDescription), (size_t)8);
    CHECK((std::is_base_of<UI::Element, UI::BorderElement>::value));
    CHECK((std::is_base_of<UI::Element, UI::ClipRegionElement>::value));
    CHECK((std::is_base_of<UI::ElementDescription, UI::BorderDescription>::value));
    CHECK((std::is_base_of<UI::ElementDescription, UI::ClipRegionDescription>::value));
    CHECK(!std::is_abstract<UI::BorderElement>::value);      // B.3.9 UI-A: concrete
    CHECK(std::is_destructible<UI::BorderElement>::value);  // B3X W1: public virtual dtor
    CHECK(!std::is_abstract<UI::ClipRegionElement>::value);      // B.3.9 UI-A: concrete
    CHECK(std::is_destructible<UI::ClipRegionElement>::value);  // B3X W1: public virtual dtor
    CHECK(!std::is_abstract<UI::BorderDescription>::value);      // B.3.9 UI-A: concrete
    CHECK(std::is_destructible<UI::BorderDescription>::value);  // B3X W1: public virtual dtor
    CHECK(!std::is_abstract<UI::ClipRegionDescription>::value);      // B.3.9 UI-A: concrete
    CHECK(std::is_destructible<UI::ClipRegionDescription>::value);  // B3X W1: public virtual dtor
    CHECK(std::has_virtual_destructor<UI::BorderElement>::value);
    CHECK(std::has_virtual_destructor<UI::ClipRegionElement>::value);

    // Nine 0x10-byte sprites, row major, fill BorderElement to its end.
    CHECK_EQ(offsetof(UI::BorderElement, _bottomRight) + 0x10, sizeof(UI::BorderElement));

    CHECK_EQ(sizeof(UI::BorderDescription), sizeof(RtUI::BorderDescription));
    CHECK_EQ(sizeof(UI::ClipRegionDescription), sizeof(RtUI::ClipRegionDescription));
    // The three names ClipRegionDescription hides live elsewhere in the base.
}

UNIT_TEST(ui_c1_borderelement_slot_order_is_retails) {
    FactoryScope scope;
    BorderElementProbe probe{};
    BareElementProbe element{};
    void* raw = static_cast<void*>(&probe);
    const void* const* vt = VTableOf(raw);
    const void* const* ev = VTableOf(static_cast<void*>(&element));
    for (int off : { kE1_TypeName, kE2_Type, kE4_ComputeSize, kE5_ComputeLayout, kE6_Draw })
        ExpectElementSlot(raw, off, kBorderE);
    for (int off : { kE3_Persist, kE7_Update, kE8_Enable, kE9_Contains, kE10_SetColor,
                     kE11_GetBorder })
        CHECK(vt[Retail(off / 8)] == ev[Retail(off / 8)]);
}

UNIT_TEST(ui_c1_clipregionelement_slot_order_is_retails) {
    FactoryScope scope;
    ClipRegionElementProbe probe{};
    BareElementProbe element{};
    void* raw = static_cast<void*>(&probe);
    const void* const* vt = VTableOf(raw);
    const void* const* ev = VTableOf(static_cast<void*>(&element));
    for (int off : { kE1_TypeName, kE2_Type, kE4_ComputeSize, kE5_ComputeLayout, kE6_Draw })
        ExpectElementSlot(raw, off, kClipE);
    void* handle = nullptr;
    UI::Element::UpdateState state;
    Reset();
    SlotAt<void (*)(void*, const void*, void*, void**, const void*)>(raw, kE7_Update)(
        raw, nullptr, nullptr, &handle, &state);
    CHECK_EQ(g_who, (int)kClipE); CHECK_EQ(g_slot, 7);
    // Slot 9 Contains is ClipRegion's own (the only one in this block).
    CHECK(vt[Retail(kE9_Contains / 8)] != ev[Retail(kE9_Contains / 8)]);
    for (int off : { kE3_Persist, kE8_Enable, kE10_SetColor, kE11_GetBorder })
        CHECK(vt[Retail(off / 8)] == ev[Retail(off / 8)]);
}

UNIT_TEST(ui_c1_border_clipregion_description_slots) {
    BorderDescriptionProbe border;
    ClipRegionDescriptionProbe clip;
    BareExternalProbe bare;
    const void* compile = RetailEntry<Object::Base>(static_cast<void*>(&bare), kD4_Compile / 8);
    for (int off : { kD1_TypeName, kD2_Type, kD3_GetFileVersion, kD5_Serialize,
                     kD6_Reconstruct, kD7_Destruct, kD9_Construct }) {
        ExpectDescriptionSlot(static_cast<void*>(&border), off, kBorderD);
        ExpectDescriptionSlot(static_cast<void*>(&clip), off, kClipD);
    }
    CHECK(RetailEntry<Object::Base>(static_cast<void*>(&border), kD4_Compile / 8) == compile);
    CHECK(RetailEntry<Object::Base>(static_cast<void*>(&clip), kD4_Compile / 8) == compile);
    ExpectCreateInstanceRetslot(static_cast<void*>(&border), kBorderD, 0xBDBDBDBDu);
    ExpectCreateInstanceRetslot(static_cast<void*>(&clip), kClipD, 0xC1C2C1C2u);
}

namespace {
int g_containsDoorEntries = 0;
bool MintedClipContainsDoor(const void* self, int x, int y, void* ui) {
    ++g_containsDoorEntries;
    return reinterpret_cast<const UI::ClipRegionElement*>(self)            // B.3.6 bridge
        ->UI::ClipRegionElement::Contains(x, y, static_cast<UI::UserInterface*>(ui));
}
}  // namespace

UNIT_TEST(ui_c1_clipregion_contains_reaches_the_base_without_reentry) {
    FactoryScope scope;
    ClipRegionElementProbe probe{};
    void* raw = static_cast<void*>(&probe);

    const void* minted[13] = {};   // twelve retail slots = thirteen compiler slots
    const void* const* real = VTableOf(raw);
    for (int i = 0; i < 13; ++i) minted[i] = real[i];

    minted[RetailSlot<Object::Base>(kE9_Contains / 8)] = reinterpret_cast<const void*>(&MintedClipContainsDoor);
    *reinterpret_cast<const void* const**>(raw) = minted;

    g_containsDoorEntries = 0;
    Reset();
    // Element::Contains is ui_element_abi_test.cpp's marker, which returns
    // true: a true here is the base body answering, reached exactly once.
    const bool r = SlotAt<bool (*)(const void*, int, int, void*)>(raw, kE9_Contains)(
        raw, 3, 4, nullptr);
    CHECK(r);
    CHECK_EQ(g_containsDoorEntries, 1);
    CHECK_EQ(g_trailN, 1);
    CHECK_EQ(g_trail[0], kClipE * 100 + 9);

    *reinterpret_cast<const void* const**>(raw) = real;
}

// ===========================================================================
// ENDGAME B.3.9 UI-A (B39_RULES D1/D2, rule B9-16) — the CONCRETE classes.
// ===========================================================================
namespace {

// Build three elements behind an array cookie, run the ARRAY arm of slot 0 and
// check it destroys downward, frees and returns the block; then the scalar
// arm with a free. `Make` placement-constructs one real object.
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

UNIT_TEST(b39_uia_border_slot_order_by_pmf) {
    using BE = UI::BorderElement;
    CHECK_DTOR_SLOT(BE);   // 0x1800414f0
    CHECK_SLOT(BE, TypeName,      0x08/8);
    CHECK_SLOT(BE, Type,          0x10/8);
    CHECK_SLOT(BE, ComputeSize,   0x20/8);
    CHECK_SLOT(BE, ComputeLayout, 0x28/8);
    CHECK_SLOT(BE, Draw,          0x30/8);
    using BD = UI::BorderDescription;
    CHECK_DTOR_SLOT(BD);  // 0x180040ff0
    CHECK_SLOT(BD, TypeName,       0x08/8);
    CHECK_SLOT(BD, Type,           0x10/8);
    CHECK_SLOT(BD, GetFileVersion, 0x18/8);
    CHECK_SLOT(BD, Serialize,      0x28/8);
    CHECK_SLOT(BD, Reconstruct,    0x30/8);
    CHECK_SLOT(BD, Destruct,       0x38/8);
    CHECK_SLOT(BD, CreateInstance, 0x40/8);
    CHECK_SLOT(BD, Construct,      0x48/8);
}

// ??0BorderElement 0x18010fd40: Element(), then nine {sheet 0, index -1}; each
// Sprite's pad dword untouched. The compiler's table, one per class.
UNIT_TEST(b39_uia_borderelement_ctor_makes_retails_stores) {
    FactoryScope scope;
    alignas(8) uint8_t buf[0x150];
    std::memset(buf, 0xCD, sizeof buf);
    UI::BorderElement* e = ::new (buf) UI::BorderElement();
    CHECK(static_cast<void*>(e) == static_cast<void*>(buf));
    CHECK_EQ(e->_alignment, 3);                    // Element() ran (marker)
    const ::Sprite* s = &e->_topLeft;
    for (int i = 0; i < 9; ++i) {
        CHECK(s[i].spriteSheet == nullptr);
        CHECK_EQ(s[i].spriteIndex, -1);
        CHECK_EQ((uint32_t)s[i].pad0C, 0xCDCDCDCDu);
    }
    BorderElementProbe probe{};
    alignas(8) uint8_t buf2[0x150];
    UI::BorderElement* e2 = ::new (buf2) UI::BorderElement();
    CHECK(VTableOf(e) == VTableOf(e2));
    CHECK(VTableOf(e) != VTableOf(&probe));
    g_b39DtorCount = 0;
    e->~BorderElement();
    e2->~BorderElement();
    CHECK_EQ(g_b39DtorCount, 2);
}

// ??0BorderDescription 0x18010fa50: ElementDescription(), then +0x68..+0xb0 zero.
UNIT_TEST(b39_uia_borderdescription_ctor_makes_retails_stores) {
    alignas(8) uint8_t buf[0xb8];
    std::memset(buf, 0xCD, sizeof buf);
    UI::BorderDescription* d = ::new (buf) UI::BorderDescription();
    CHECK_EQ(d->_id, -1);                          // ElementDescription() ran (marker)
    CHECK(d->_spriteSheet == nullptr);
    for (int i = 0; i < 9; ++i) CHECK(d->_spriteName[i]._data == nullptr);
    BorderDescriptionProbe probe;
    CHECK(VTableOf(d) != VTableOf(&probe));
    g_b39DtorCount = 0;
    d->~BorderDescription();
    CHECK_EQ(g_b39DtorCount, 1);
}

// Retired vector arms: BorderElement 0x1800414f0, BorderDescription 0x180040ff0.
UNIT_TEST(b39_uia_border_deletingdtor_both_arms) {
    FactoryScope scope;
    B39ExpectBothArms<UI::BorderElement>(0x150, [](void* p) { return ::new (p) UI::BorderElement(); });
    B39ExpectBothArms<UI::BorderDescription>(0xb8, [](void* p) { return ::new (p) UI::BorderDescription(); });
}

UNIT_TEST(b39_uia_clipregion_slot_order_by_pmf) {
    using CE = UI::ClipRegionElement;
    CHECK_DTOR_SLOT(CE);   // 0x180038370
    CHECK_SLOT(CE, TypeName,      0x08/8);
    CHECK_SLOT(CE, Type,          0x10/8);
    CHECK_SLOT(CE, ComputeSize,   0x20/8);
    CHECK_SLOT(CE, ComputeLayout, 0x28/8);
    CHECK_SLOT(CE, Draw,          0x30/8);
    CHECK_SLOT(CE, Update,        0x38/8);
    CHECK_SLOT(CE, Contains,      0x48/8);
    using CD = UI::ClipRegionDescription;
    CHECK_DTOR_SLOT(CD);  // 0x180037eb0
    CHECK_SLOT(CD, GetFileVersion, 0x18/8);
    CHECK_SLOT(CD, Serialize,      0x28/8);
    CHECK_SLOT(CD, Reconstruct,    0x30/8);
    CHECK_SLOT(CD, Destruct,       0x38/8);
    CHECK_SLOT(CD, CreateInstance, 0x40/8);
    CHECK_SLOT(CD, Construct,      0x48/8);
}

// ??0ClipRegionElement 0x1800cbe60: Element(), then 100, 100, 0, 0, 0, 0.
// ??0ClipRegionDescription 0x1800cc140: ElementDescription(), 0x64, 0x64, null.
UNIT_TEST(b39_uia_clipregion_ctors_make_retails_stores) {
    FactoryScope scope;
    alignas(8) uint8_t buf[0xd8];
    std::memset(buf, 0xCD, sizeof buf);
    UI::ClipRegionElement* e = ::new (buf) UI::ClipRegionElement();
    CHECK_EQ(e->_alignment, 3);
    CHECK_EQ(e->_reqWidth, 100);
    CHECK_EQ(e->_reqHeight, 100);
    CHECK_EQ(e->_innerWidth, 0);
    CHECK_EQ(e->_innerHeight, 0);
    CHECK_EQ(e->_offsetX, 0);
    CHECK_EQ(e->_offsetY, 0);
    ClipRegionElementProbe probe{};
    CHECK(VTableOf(e) != VTableOf(&probe));
    g_b39DtorCount = 0;
    e->~ClipRegionElement();
    CHECK_EQ(g_b39DtorCount, 1);

    alignas(8) uint8_t dbuf[0x78];
    std::memset(dbuf, 0xCD, sizeof dbuf);
    UI::ClipRegionDescription* d = ::new (dbuf) UI::ClipRegionDescription();
    CHECK_EQ(d->_id, -1);
    CHECK_EQ(d->_width, 0x64);
    CHECK_EQ(d->_height, 0x64);
    CHECK(d->_childDesc == nullptr);
    g_b39DtorCount = 0;
    d->~ClipRegionDescription();
    CHECK_EQ(g_b39DtorCount, 1);
}

// Retired vector arms: ClipRegionElement 0x180038370, ClipRegionDescription 0x180037eb0.
UNIT_TEST(b39_uia_clipregion_deletingdtor_both_arms) {
    FactoryScope scope;
    B39ExpectBothArms<UI::ClipRegionElement>(0xd8, [](void* p) { return ::new (p) UI::ClipRegionElement(); });
    B39ExpectBothArms<UI::ClipRegionDescription>(0x78, [](void* p) { return ::new (p) UI::ClipRegionDescription(); });
}

UNIT_TEST(b39_uia_button_family_slot0_by_pmf) {
    CHECK_DTOR_SLOT(UI::ButtonElement);   // 0x1800485b0
    CHECK_SLOT(UI::ButtonElement, Enable, 0x40/8);
    CHECK_DTOR_SLOT(UI::CheckElement);   // 0x180048d80
    CHECK_SLOT(UI::CheckElement, Update, 0x38/8);
    CHECK_DTOR_SLOT(UI::ComboElement);   // 0x180075390
    CHECK_SLOT(UI::ComboElement, Update, 0x38/8);
    CHECK_DTOR_SLOT(UI::ButtonDescription);   // 0x1800481a0
    CHECK_SLOT(UI::ButtonDescription, Construct, 0x48/8);
    CHECK_DTOR_SLOT(UI::CheckDescription);   // 0x1800489c0
    CHECK_SLOT(UI::CheckDescription, Construct, 0x48/8);
    CHECK_DTOR_SLOT(UI::ComboDescription);   // 0x180074e70
    CHECK_SLOT(UI::ComboDescription, Construct, 0x48/8);
}

// The real ctors' stores; untouched bytes stay 0xCD.
UNIT_TEST(b39_uia_button_family_ctors_make_retails_stores) {
    FactoryScope scope;
    {
        alignas(8) uint8_t buf[0xe0];
        std::memset(buf, 0xCD, sizeof buf);
        UI::CheckElement* e = ::new (buf) UI::CheckElement();
        CHECK_EQ(e->_alignment, 3);
        CHECK_EQ(e->_keyIndex, -1);
        CHECK_EQ(static_cast<int32_t>(e->_state),
                 static_cast<int32_t>(UI::ButtonElement::ButtonState::Normal));
        for (int i = 0; i < 4; ++i) CHECK_EQ(e->_stateColor[i], 0xCDCDCDCDu);   // NOT written
        CHECK_EQ((int)e->_checked, 0);
        CHECK_EQ((int)e->_padD9[0], 0xCD);
        CheckElementProbe probe{};
        CHECK(VTableOf(e) != VTableOf(&probe));
        g_b39DtorCount = 0;
        e->~CheckElement();
        CHECK_EQ(g_b39DtorCount, 2);                  // ~CheckElement, ~ButtonElement
    }
    {
        alignas(8) uint8_t buf[0x128];
        std::memset(buf, 0xCD, sizeof buf);
        UI::ComboElement* e = ::new (buf) UI::ComboElement();
        CHECK_EQ(e->_keyIndex, -1);
        CHECK(e->_listDesc == nullptr);
        CHECK(e->_itemDesc == nullptr);
        CHECK(e->_strings._data == nullptr);
        CHECK_EQ(e->_strings._size, 0);
        CHECK(e->_sprites._data == nullptr);
        CHECK(e->_userData._data == nullptr);
        CHECK_EQ(e->_currentItem, -1);
        CHECK_EQ(e->_pad11c, 0xCDCDCDCDu);
        CHECK(e->_openDialog == nullptr);
        g_b39DtorCount = 0;
        e->~ComboElement();
        CHECK_EQ(g_b39DtorCount, 2);                  // ~ComboElement, ~ButtonElement
    }
    {
        alignas(8) uint8_t buf[0xb8];
        std::memset(buf, 0xCD, sizeof buf);
        UI::ComboDescription* d = ::new (buf) UI::ComboDescription();
        CHECK_EQ(d->_id, -1);
        for (int i = 0; i < 5; ++i) CHECK(d->_childDesc[i] == nullptr);
        CHECK_EQ(d->_enabledColor, 0); CHECK_EQ(d->_pressedColor, 0);
        CHECK(d->_listDesc == nullptr);
        CHECK(d->_itemDesc == nullptr);
        CHECK(d->_stringTable == nullptr);
        g_b39DtorCount = 0;
        d->~ComboDescription();
        CHECK_EQ(g_b39DtorCount, 2);                  // ~ComboDescription, ~ButtonDescription
    }
    {
        alignas(8) uint8_t buf[0xa8];
        std::memset(buf, 0xCD, sizeof buf);
        UI::CheckDescription* d = ::new (buf) UI::CheckDescription();
        CHECK(d->_childDesc[4] == nullptr);
        CHECK(d->_checkDesc == nullptr);
        d->~CheckDescription();
    }
}

// Retired vector arms: ButtonElement 0x1800485b0, ButtonDescription 0x1800481a0.
UNIT_TEST(b39_uia_button_family_deletingdtor_both_arms) {
    FactoryScope scope;
    B39ExpectBothArms<UI::ButtonElement>(0xd8, [](void* p) { return ::new (p) UI::ButtonElement(); });
    B39ExpectBothArms<UI::ButtonDescription>(0xa0, [](void* p) { return ::new (p) UI::ButtonDescription(); });
}
