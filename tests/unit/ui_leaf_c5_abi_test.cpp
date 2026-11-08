// ============================================================================
// tests/unit/ui_leaf_c5_abi_test.cpp — ENDGAME B.3.6 block C5
//   the seven leaf pairs as real C++ classes:
//   DocumentText, Graph, Progress, Ribbon, Slider, Spinner, Tab
//   (`UI::XDescription : UI::ElementDescription`, 10 slots;
//    `UI::XElement : UI::Element`, 12 slots)
// ============================================================================
//
// PART A — LAYOUT. Retail `sizeof` (the `MOV EDX,imm` in each class's slot-0
//   vector deleting destructor, B36_SLOTS.md section 1), `alignof`, abstract,
//   no virtual destructor, derived fields starting at sizeof(base).
//
// PART B — SLOT ORDER. A probe subclass of each leaf overrides only slot 0;
//   every slot the leaf OWNS is raw-called at its retail byte offset and must
//   land in that leaf's member (the markers below); every slot the leaf
//   INHERITS must carry the very pointer a bare-base probe carries. That pins
//   `Construct` at slot 9 BEHIND `CreateInstance` (RULE B6-1.1), `Compile`
//   inherited at slot 4 (B6-1.5), and which Elements override `Update`.
//
// PART C — the two class-valued slots, raw-called the RETAIL way: every
//   Description's slot 8 `CreateInstance` takes the hidden return slot in RDX
//   and the Dialog in R8 and hands the slot back in RAX (RULE B6-2); the
//   Elements that override slot 7 `Update` receive the ADDRESS of the caller's
//   `InternalPtr<DialogInstance>` temporary (Element.h's banner).
//
// WHAT IS NOT PROVEN HERE: the ported bodies (src/engine/ui/*.cpp do not link
// into the unit build) — those are verified by the real run. The members are
// defined below as MARKERS.
// ============================================================================
#include "unit.h"
#include "slot_check.h"
#include "b39_uib_test_util.h"   // ENDGAME B.3.9 UI-B: the concrete-class checks

#include "ui/Element.h"
#include "ui/ElementDescription.h"
#include "ui/GraphDescription.h"
#include "ui/GraphElement.h"
#include "ui/ProgressDescription.h"
#include "ui/ProgressElement.h"
#include "ui/SpinnerDescription.h"
#include "ui/SpinnerElement.h"
#include "ui/SliderDescription.h"
#include "ui/SliderElement.h"
#include "ui/RibbonDescription.h"
#include "ui/RibbonElement.h"
#include "ui/TabDescription.h"
#include "ui/TabElement.h"
#include "ui/DocumentTextDescription.h"
#include "ui/DocumentTextElement.h"
#include "ui/DialogInstance.h"   // B4 S4 RT2: InternalPtr<DialogInstance>::Get() needs the class complete

#include <cstddef>
#include <cstdint>
#include <cstring>
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
// Observation channel: which class's member, which slot, on which object.
// ---------------------------------------------------------------------------
enum Cls : int {
    kNone = 0,
    kGraphD, kGraphE,
    kProgressD, kProgressE,
    kSpinnerD, kSpinnerE,
    kSliderD, kSliderE,
    kRibbonD, kRibbonE,
    kTabD, kTabE,
    kDocTextD, kDocTextE,
};
const void* g_self = nullptr;
int         g_cls  = kNone;
int         g_slot = -1;
long long   g_arg  = 0;

void Hit(const void* self, int cls, int slot, long long arg = 0) {
    g_self = self; g_cls = cls; g_slot = slot; g_arg = arg;
}
void Reset() { g_self = nullptr; g_cls = kNone; g_slot = -1; g_arg = 0; }

// The offset / index is RETAIL's; T decides the compiler's. Default T: every object this file reads raw is a UI class, all Object::Base-rooted
// (a virtual destructor is inherited, so the root answers for every class).
template <typename Fn, class T = Object::Base>
Fn SlotAt(const void* obj, int index) {
    const void* const* vt = *reinterpret_cast<const void* const* const*>(obj);
    Fn f = nullptr;
    const void* raw = vt[RetailSlot<T>(index)];
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

const void* const kFakeElement = reinterpret_cast<const void*>(static_cast<uintptr_t>(0xE1E1E1E1));
const void* const kFakeDialog  = reinterpret_cast<const void*>(static_cast<uintptr_t>(0xD1A1));
const void* const kFakeHandle  = reinterpret_cast<const void*>(static_cast<uintptr_t>(0x7000));

// ---------------------------------------------------------------------------
// Bare-base probes: their inherited slots are the BASE members' pointers.
// ---------------------------------------------------------------------------
struct BaseElementProbe final : UI::Element {
    void ComputeSize() override {}
};
struct BaseDescriptionProbe final : UI::ElementDescription {
    Object::External* Reconstruct() override { return this; }
    void Destruct() override {}
    Object::InternalPtr<UI::Element>* CreateInstance(
            Object::InternalPtr<UI::Element>* ret, const UI::Dialog*) const override {
        return ret;
    }
};

// Slots 1/2 (ENDGAME B.3.9 TI-UI): DECLARE_TYPE's inline bodies over the
// class's own record -- checked by what they return, not by a marker.
const ::Object::BaseTypeInfo* WhoRecord(int cls);
void CheckOwnTypeSlots(const void* raw, int cls) {
    CHECK(SlotAt<const char* (*)(const void*)>(raw, 1)(raw) == WhoRecord(cls)->name);
    CHECK_EQ(SlotAt<uint32_t (*)(const void*)>(raw, 2)(raw), WhoRecord(cls)->typeId);
}

// Raw-call one Element slot with retail-shaped arguments.
void CallElementSlot(void* raw, int slot) {
    switch (slot) {
    case 1:  SlotAt<const char* (*)(const void*)>(raw, 1)(raw); break;
    case 2:  SlotAt<uint32_t (*)(const void*)>(raw, 2)(raw); break;
    case 3:  SlotAt<void (*)(void*, void*)>(raw, 3)(raw, nullptr); break;
    case 4:  SlotAt<void (*)(void*)>(raw, 4)(raw); break;
    case 5:  SlotAt<void (*)(void*, int, int, int, int)>(raw, 5)(raw, 7, 0, 0, 9); break;
    case 6:  SlotAt<void (*)(const void*, void*, uint8_t)>(raw, 6)(raw, nullptr, 200); break;
    case 7: {
        // Retail passes the ADDRESS of the caller's handle temporary in R9.
        const void* handle = kFakeHandle;
        UI::Element::UpdateState state;
        state.mouseX = 5;
        SlotAt<void (*)(void*, const void*, void*, const void**, const void*)>(raw, 7)(
            raw, nullptr, nullptr, &handle, &state);
        break;
    }
    case 8:  SlotAt<void (*)(void*, bool)>(raw, 8)(raw, true); break;
    case 9:  SlotAt<bool (*)(const void*, int, int, void*)>(raw, 9)(raw, 3, 4, nullptr); break;
    case 10: SlotAt<void (*)(void*, uint32_t)>(raw, 10)(raw, 0x11223344u); break;
    case 11: { void* s = nullptr; SlotAt<void** (*)(const void*, void**)>(raw, 11)(raw, &s); break; }
    default: break;
    }
}
void CallDescriptionSlot(void* raw, int slot) {
    switch (slot) {
    case 1: SlotAt<const char* (*)(const void*)>(raw, 1)(raw); break;
    case 2: SlotAt<uint32_t (*)(const void*)>(raw, 2)(raw); break;
    case 3: SlotAt<int32_t (*)(const void*)>(raw, 3)(raw); break;
    case 5: SlotAt<bool (*)(void*, void*)>(raw, 5)(raw, nullptr); break;
    case 6: SlotAt<void* (*)(void*)>(raw, 6)(raw); break;
    case 7: SlotAt<void (*)(void*)>(raw, 7)(raw); break;
    case 8: { void* s = nullptr;
              SlotAt<void** (*)(const void*, void**, const void*)>(raw, 8)(raw, &s, kFakeDialog);
              break; }
    case 9: SlotAt<void* (*)(void*)>(raw, 9)(raw); break;
    default: break;
    }
}

// Element: slots in `ownMask` must hit (cls, slot); every other slot 1..11 must
// equal the bare Element probe's entry.
void CheckElementSlots(void* raw, int cls, unsigned ownMask) {
    FactoryScope scope;
    BaseElementProbe base{};
    const void* const* vb = VTableOf(&base);
    const void* const* vl = VTableOf(raw);
    for (int s = 1; s <= 11; ++s) {
        if ((ownMask & (1u << s)) && (s == 1 || s == 2)) {   // TI-UI: own record
            CheckOwnTypeSlots(raw, cls);
            CHECK(vl[Retail(s)] != vb[Retail(s)]);
        } else if (ownMask & (1u << s)) {
            Reset(); CallElementSlot(raw, s);
            CHECK_EQ(g_cls, cls); CHECK_EQ(g_slot, s); CHECK(g_self == raw);
            CHECK(vl[Retail(s)] != vb[Retail(s)]);
        } else {
            CHECK(vl[Retail(s)] == vb[Retail(s)]);
        }
    }
    CHECK(vl[Retail(4)] != vb[Retail(4)]);   // every leaf overrides (or inherits an override of) ComputeSize
}
// Description: 1,2,3,5,6,7,8,9 are the leaf's own; 4 is External::Compile.
void CheckDescriptionSlots(void* raw, int cls) {
    BaseDescriptionProbe base;
    const void* const* vb = VTableOf(&base);
    const void* const* vl = VTableOf(raw);
    const int own[] = {1, 2, 3, 5, 6, 7, 8, 9};
    for (int s : own) {
        if (s == 1 || s == 2) { CheckOwnTypeSlots(raw, cls); continue; }   // TI-UI
        Reset(); CallDescriptionSlot(raw, s);
        CHECK_EQ(g_cls, cls); CHECK_EQ(g_slot, s); CHECK(g_self == raw);
    }
    CHECK(vl[Retail(4)] == vb[Retail(4)]);   // Compile inherited (RULE B6-1.5)
    // Slot 8 is CreateInstance and 9 is Construct, not the other way round.
    Reset(); CallDescriptionSlot(raw, 8); CHECK_EQ(g_slot, 8); CHECK_EQ(g_arg, 0xD1A1);
    Reset(); CallDescriptionSlot(raw, 9); CHECK_EQ(g_slot, 9);
}
// RULE B6-2: raw-call slot 8 the retail way and check the slot comes back.
void CheckCreateInstanceRetslot(void* raw) {
    const void* const* vtBefore = VTableOf(raw);
    void* slot = nullptr;
    void** back = SlotAt<void** (*)(const void*, void**, const void*)>(raw, 8)(
        raw, &slot, kFakeDialog);
    CHECK(back == &slot);
    CHECK(slot == kFakeElement);
    CHECK(VTableOf(raw) == vtBefore);
}
// Slot 7 on a leaf that overrides Update: the member sees the handle THROUGH
// the pointer (the marker records the handle it read, plus mouseX).
void CheckUpdateTakesHandleAddress(void* raw, int cls) {
    FactoryScope scope;
    Reset(); CallElementSlot(raw, 7);
    CHECK_EQ(g_cls, cls);
    CHECK_EQ(g_arg, 0x7000 + 5);
}

// Marker helpers for the member definitions below.
Object::InternalPtr<UI::Element>* FillRet(Object::InternalPtr<UI::Element>* ret) {
    new (static_cast<void*>(ret)) Object::InternalPtr<UI::Element>(
        reinterpret_cast<UI::Element*>(const_cast<void*>(kFakeElement)));
    return ret;
}
long long UpdateArg(Object::InternalPtr<UI::DialogInstance>* dialog,
                    const UI::Element::UpdateState& state) {
    return static_cast<long long>(reinterpret_cast<intptr_t>(dialog->Get())) + state.mouseX;
}

}  // namespace

// ===========================================================================
// The members, AS MARKERS (the real bodies are in src/engine/ui/, which does
// not link here). One macro per shape so every class gets the same set.
// ===========================================================================
#define C5_DESCRIPTION_MARKERS(C, K)                                                         \
    int32_t UI::C::GetFileVersion() const { Hit(this, K, 3); return 0; }                     \
    bool UI::C::Serialize(IO::Stream&) { Hit(this, K, 5); return true; }                           \
    Object::External* UI::C::Reconstruct() { Hit(this, K, 6); return this; }                 \
    void UI::C::Destruct() { Hit(this, K, 7); }                                              \
    Object::InternalPtr<UI::Element>* UI::C::CreateInstance(                                 \
            Object::InternalPtr<UI::Element>* ret, const UI::Dialog* d) const {              \
        Hit(this, K, 8, reinterpret_cast<intptr_t>(d)); return FillRet(ret);                 \
    }                                                                                        \
    Object::External* UI::C::Construct() { Hit(this, K, 9); return this; }                   \
    UI::C::~C() { B39NoteDtor(this); }

#define C5_ELEMENT_MARKERS(C, K)                                                             \
    void UI::C::ComputeSize() { Hit(this, K, 4); }                                           \
    void UI::C::ComputeLayout(int32_t x, int32_t, int32_t, int32_t h) {                      \
        Hit(this, K, 5, (long long)x * 1000 + h);                                            \
    }                                                                                        \
    void UI::C::Draw(UI::SpriteContext&, uint8_t a) const { Hit(this, K, 6, a); }            \
    UI::C::~C() { B39NoteDtor(this); }

#define C5_ELEMENT_UPDATE_MARKER(C, K)                                                       \
    void UI::C::Update(const System::Time&, UI::UserInterface*,                              \
                       Object::InternalPtr<UI::DialogInstance>* dialog,                      \
                       const UpdateState& state) {                                           \
        Hit(this, K, 7, UpdateArg(dialog, state));                                           \
    }

C5_DESCRIPTION_MARKERS(GraphDescription, kGraphD)
C5_ELEMENT_MARKERS(GraphElement, kGraphE)

namespace {
struct GraphDescriptionProbe final : UI::GraphDescription {
};
struct GraphElementProbe final : UI::GraphElement {
};

// Slot-ownership masks for the Elements (bit n = slot n is the leaf's own).
constexpr unsigned kOwn456  = (1u << 1) | (1u << 2) | (1u << 4) | (1u << 5) | (1u << 6);
constexpr unsigned kOwn4567 = kOwn456 | (1u << 7);
}  // namespace

// ===========================================================================
// PART A — LAYOUT
// ===========================================================================
template <class C, class Base>
static void CheckShape(size_t size, bool concrete = false) {
    CHECK_EQ(sizeof(C), size);
    CHECK_EQ(alignof(C), (size_t)8);
    CHECK(std::has_virtual_destructor<C>::value);   // B3X W1: retail slot 0 = D1 + D0
    CHECK((std::is_base_of<Base, C>::value));
    if (concrete) {
        // ENDGAME B.3.9 UI-B (B39_RULES D1): public real ctor, own slot 0;
        // B3X W1: public virtual dtor.
        CHECK(B39IsConcreteShape<C>());
    } else {
        CHECK(std::is_abstract<C>::value);
        CHECK(!std::is_default_constructible<C>::value);   // protected ctor: never built as C++
    }
}

UNIT_TEST(ui_c5_graph_layout) {
    CheckShape<UI::GraphDescription, UI::ElementDescription>(0x88, true);   // 0x1800403a0
    CheckShape<UI::GraphElement, UI::Element>(0x100, true);                 // 0x1800408b0
    CHECK_EQ(offsetof(UI::GraphDescription, _width), sizeof(UI::ElementDescription));
    CHECK_EQ(offsetof(UI::GraphElement, _width), sizeof(UI::Element));
}

// ===========================================================================
// PART B / C
// ===========================================================================
UNIT_TEST(ui_c5_graph_slots) {
    GraphDescriptionProbe d;
    CheckDescriptionSlots(&d, kGraphD);
    CheckCreateInstanceRetslot(&d);

    FactoryScope scope;
    GraphElementProbe e{};
    CheckElementSlots(&e, kGraphE, kOwn456);        // Update inherited from Element

    // The three one-instruction setters (0x1800405e0 / 0x1800405f0 /
    // 0x180040600): SetYDivisions writes +0xe4, like SetXDivisions (retail).
    e._yDivisions = 4;
    e.SetYRange(2.5f);  CHECK(e._yRange == 2.5f);
    e.SetXDivisions(9); CHECK_EQ(e._xDivisions, 9);
    e.SetYDivisions(3); CHECK_EQ(e._xDivisions, 3); CHECK_EQ(e._yDivisions, 4);
}

// ===========================================================================
// Progress
// ===========================================================================
C5_DESCRIPTION_MARKERS(ProgressDescription, kProgressD)
C5_ELEMENT_MARKERS(ProgressElement, kProgressE)

namespace {
struct ProgressDescriptionProbe final : UI::ProgressDescription {
};
struct ProgressElementProbe final : UI::ProgressElement {
};
}  // namespace

UNIT_TEST(ui_c5_progress_layout) {
    CheckShape<UI::ProgressDescription, UI::ElementDescription>(0xb0, true);   // 0x180077c80
    CheckShape<UI::ProgressElement, UI::Element>(0x130, true);                 // 0x180078150
    CHECK_EQ(offsetof(UI::ProgressDescription, _backgroundColor), sizeof(UI::ElementDescription));
    CHECK_EQ(offsetof(UI::ProgressElement, _fillLeft), sizeof(UI::Element));
    CHECK_EQ(offsetof(UI::ProgressElement, _backgroundColor), (size_t)0x120);
}

UNIT_TEST(ui_c5_progress_slots) {
    ProgressDescriptionProbe d;
    CheckDescriptionSlots(&d, kProgressD);
    CheckCreateInstanceRetslot(&d);

    FactoryScope scope;
    ProgressElementProbe e{};
    CheckElementSlots(&e, kProgressE, kOwn456);   // Update inherited from Element
    e.SetMaximumValue(2.0f);        CHECK(e._maxValue == 2.0f);        // 0x180077ec0
    e.SetCurrentValue(0.5f);        CHECK(e._currentValue == 0.5f);    // 0x180077ed0
    e.SetBackgroundColor(0xAABBCCDDu); CHECK_EQ(e._backgroundColor, 0xAABBCCDDu);  // 0x180077ee0
}

// ===========================================================================
// Spinner — the Element OVERRIDES slot 7 Update
// ===========================================================================
C5_DESCRIPTION_MARKERS(SpinnerDescription, kSpinnerD)
C5_ELEMENT_MARKERS(SpinnerElement, kSpinnerE)
C5_ELEMENT_UPDATE_MARKER(SpinnerElement, kSpinnerE)

namespace {
struct SpinnerDescriptionProbe final : UI::SpinnerDescription {
};
struct SpinnerElementProbe final : UI::SpinnerElement {
};
}  // namespace

UNIT_TEST(ui_c5_spinner_layout) {
    CheckShape<UI::SpinnerDescription, UI::ElementDescription>(0x88, true);   // 0x18004b050
    CheckShape<UI::SpinnerElement, UI::Element>(0xc8, true);                  // 0x18004b3b0
    CHECK_EQ(offsetof(UI::SpinnerDescription, _editDescription), sizeof(UI::ElementDescription));
    CHECK_EQ(offsetof(UI::SpinnerElement, _increment), sizeof(UI::Element));
}

UNIT_TEST(ui_c5_spinner_slots) {
    SpinnerDescriptionProbe d;
    CheckDescriptionSlots(&d, kSpinnerD);
    CheckCreateInstanceRetslot(&d);

    FactoryScope scope;
    SpinnerElementProbe e{};
    CheckElementSlots(&e, kSpinnerE, kOwn4567);    // Update overridden
    CheckUpdateTakesHandleAddress(&e, kSpinnerE);
    e.SetIncrement(5); CHECK_EQ(e._increment, 5);   // 0x1800d7d30
}

// ===========================================================================
// Slider — the Element OVERRIDES slot 7 Update
// ===========================================================================
C5_DESCRIPTION_MARKERS(SliderDescription, kSliderD)
C5_ELEMENT_MARKERS(SliderElement, kSliderE)
C5_ELEMENT_UPDATE_MARKER(SliderElement, kSliderE)

namespace {
struct SliderDescriptionProbe final : UI::SliderDescription {
};
struct SliderElementProbe final : UI::SliderElement {
};
}  // namespace

UNIT_TEST(ui_c5_slider_layout) {
    CheckShape<UI::SliderDescription, UI::ElementDescription>(0xb8, true);   // 0x180049c60
    CheckShape<UI::SliderElement, UI::Element>(0x130, true);                 // 0x18004a180
    CHECK_EQ(offsetof(UI::SliderDescription, _childDescription), sizeof(UI::ElementDescription));
    CHECK_EQ(offsetof(UI::SliderElement, _trackStart), sizeof(UI::Element));
    CHECK_EQ(offsetof(UI::SliderElement, _value), (size_t)UI::SliderElementOffsets::kValue);
}

UNIT_TEST(ui_c5_slider_slots) {
    SliderDescriptionProbe d;
    CheckDescriptionSlots(&d, kSliderD);
    CheckCreateInstanceRetslot(&d);

    FactoryScope scope;
    SliderElementProbe e{};
    CheckElementSlots(&e, kSliderE, kOwn4567);     // Update overridden
    CheckUpdateTakesHandleAddress(&e, kSliderE);
    e.SetMinimumValue(-1.0f); CHECK(e._minimum == -1.0f);   // 0x180049ea0
    e.SetMaximumValue(3.0f);  CHECK(e._maximum == 3.0f);    // 0x180049eb0
    e.SetCurrentValue(0.5f);  CHECK(e._value == 0.5f);      // 0x180049ec0
    CHECK(e.GetCurrentValue() == 0.5f);                     // 0x180049ed0
}

// ===========================================================================
// Ribbon
// ===========================================================================
C5_DESCRIPTION_MARKERS(RibbonDescription, kRibbonD)
C5_ELEMENT_MARKERS(RibbonElement, kRibbonE)

namespace {
struct RibbonDescriptionProbe final : UI::RibbonDescription {
};
struct RibbonElementProbe final : UI::RibbonElement {
};
}  // namespace

UNIT_TEST(ui_c5_ribbon_layout) {
    CheckShape<UI::RibbonDescription, UI::ElementDescription>(0x80, true);   // 0x180044620
    CheckShape<UI::RibbonElement, UI::Element>(0xd8, true);                  // 0x180044ac0
    CHECK_EQ(offsetof(UI::RibbonDescription, _vertical), sizeof(UI::ElementDescription));
    CHECK_EQ(offsetof(UI::RibbonElement, _vertical), sizeof(UI::Element));
}

UNIT_TEST(ui_c5_ribbon_slots) {
    RibbonDescriptionProbe d;
    CheckDescriptionSlots(&d, kRibbonD);
    CheckCreateInstanceRetslot(&d);

    FactoryScope scope;
    RibbonElementProbe e{};
    CheckElementSlots(&e, kRibbonE, kOwn456);      // Update inherited from Element
}

// ===========================================================================
// Tab — the Element OVERRIDES slot 7 Update
// ===========================================================================
C5_DESCRIPTION_MARKERS(TabDescription, kTabD)
C5_ELEMENT_MARKERS(TabElement, kTabE)
C5_ELEMENT_UPDATE_MARKER(TabElement, kTabE)

namespace {
struct TabDescriptionProbe final : UI::TabDescription {
};
struct TabElementProbe final : UI::TabElement {
};
}  // namespace

UNIT_TEST(ui_c5_tab_layout) {
    CheckShape<UI::TabDescription, UI::ElementDescription>(0xb8, true);   // 0x18004c640
    CheckShape<UI::TabElement, UI::Element>(0x108, true);                 // 0x18004ca90
    CHECK_EQ(offsetof(UI::TabDescription, _stripBackdropDescription), sizeof(UI::ElementDescription));
    CHECK_EQ(offsetof(UI::TabElement, _tabCount), sizeof(UI::Element));
    CHECK_EQ(offsetof(UI::TabElement, _activeTab), (size_t)UI::TabElementOffsets::kActiveTab);
}

UNIT_TEST(ui_c5_tab_slots) {
    TabDescriptionProbe d;
    CheckDescriptionSlots(&d, kTabD);
    CheckCreateInstanceRetslot(&d);

    FactoryScope scope;
    TabElementProbe e{};
    CheckElementSlots(&e, kTabE, kOwn4567);         // Update overridden
    CheckUpdateTakesHandleAddress(&e, kTabE);
    e._activeTab = 3; CHECK_EQ(e.GetActiveTab(), 3);   // 0x18004c880
}

// ===========================================================================
// DocumentText — the Element OVERRIDES slot 7 Update
// ===========================================================================
C5_DESCRIPTION_MARKERS(DocumentTextDescription, kDocTextD)
C5_ELEMENT_MARKERS(DocumentTextElement, kDocTextE)

// ENDGAME B.3.9 TI-UI: the fourteen records (literals as src/engine/ui passes
// them) and the null factory pair they point at; the factories are never
// called here.
#define C5_TEST_FACTORIES(CLS)                                              \
    ::Object::Base* UI::CLS::CreateNewObject() { return nullptr; }              \
    ::Object::Base* UI::CLS::CreateInPlaceObject(void*) { return nullptr; }
C5_TEST_FACTORIES(GraphDescription)
DEFINE_TYPE(UI::GraphDescription, "ElementDescription", nullptr)
C5_TEST_FACTORIES(GraphElement)
DEFINE_TYPE(UI::GraphElement, "Element", nullptr)
C5_TEST_FACTORIES(ProgressDescription)
DEFINE_TYPE(UI::ProgressDescription, "ElementDescription", nullptr)
C5_TEST_FACTORIES(ProgressElement)
DEFINE_TYPE(UI::ProgressElement, "Element", nullptr)
C5_TEST_FACTORIES(SpinnerDescription)
DEFINE_TYPE(UI::SpinnerDescription, "ElementDescription", nullptr)
C5_TEST_FACTORIES(SpinnerElement)
DEFINE_TYPE(UI::SpinnerElement, "Element", nullptr)
C5_TEST_FACTORIES(SliderDescription)
DEFINE_TYPE(UI::SliderDescription, "ElementDescription", nullptr)
C5_TEST_FACTORIES(SliderElement)
DEFINE_TYPE(UI::SliderElement, "Element", nullptr)
C5_TEST_FACTORIES(RibbonDescription)
DEFINE_TYPE(UI::RibbonDescription, "ElementDescription", nullptr)
C5_TEST_FACTORIES(RibbonElement)
DEFINE_TYPE(UI::RibbonElement, "Element", nullptr)
C5_TEST_FACTORIES(TabDescription)
DEFINE_TYPE(UI::TabDescription, "ElementDescription", nullptr)
C5_TEST_FACTORIES(TabElement)
DEFINE_TYPE(UI::TabElement, "Element", nullptr)
C5_TEST_FACTORIES(DocumentTextDescription)
DEFINE_TYPE(UI::DocumentTextDescription, "ElementDescription", nullptr)
C5_TEST_FACTORIES(DocumentTextElement)
DEFINE_TYPE(UI::DocumentTextElement, "Element", nullptr)
#undef C5_TEST_FACTORIES
namespace {
const ::Object::BaseTypeInfo* WhoRecord(int cls) {
    switch (cls) {
    case kGraphD: return &UI::GraphDescription::GetTypeInfo();
    case kGraphE: return &UI::GraphElement::GetTypeInfo();
    case kProgressD: return &UI::ProgressDescription::GetTypeInfo();
    case kProgressE: return &UI::ProgressElement::GetTypeInfo();
    case kSpinnerD: return &UI::SpinnerDescription::GetTypeInfo();
    case kSpinnerE: return &UI::SpinnerElement::GetTypeInfo();
    case kSliderD: return &UI::SliderDescription::GetTypeInfo();
    case kSliderE: return &UI::SliderElement::GetTypeInfo();
    case kRibbonD: return &UI::RibbonDescription::GetTypeInfo();
    case kRibbonE: return &UI::RibbonElement::GetTypeInfo();
    case kTabD: return &UI::TabDescription::GetTypeInfo();
    case kTabE: return &UI::TabElement::GetTypeInfo();
    case kDocTextD: return &UI::DocumentTextDescription::GetTypeInfo();
    case kDocTextE: return &UI::DocumentTextElement::GetTypeInfo();
    default: return nullptr;
    }
}
}  // namespace
C5_ELEMENT_UPDATE_MARKER(DocumentTextElement, kDocTextE)

namespace {
struct DocumentTextDescriptionProbe final : UI::DocumentTextDescription {
};
struct DocumentTextElementProbe final : UI::DocumentTextElement {
};
}  // namespace

UNIT_TEST(ui_c5_documenttext_layout) {
    CheckShape<UI::DocumentTextDescription, UI::ElementDescription>(0xe8, true);   // 0x18003bd90
    CheckShape<UI::DocumentTextElement, UI::Element>(0x1c8, true);                 // 0x18003c66f
    CHECK_EQ(offsetof(UI::DocumentTextDescription, _documentStringTable),
             sizeof(UI::ElementDescription));
    CHECK_EQ(offsetof(UI::DocumentTextElement, _text), sizeof(UI::Element));
    CHECK_EQ(offsetof(UI::DocumentTextElement, _links),
             (size_t)UI::DocumentTextElementOffsets::kLinkArrayData);
    CHECK_EQ(offsetof(UI::DocumentTextElement, _sourceText),
             (size_t)UI::DocumentTextElementOffsets::kContentString);
}

UNIT_TEST(ui_c5_documenttext_slots) {
    DocumentTextDescriptionProbe d;
    CheckDescriptionSlots(&d, kDocTextD);
    CheckCreateInstanceRetslot(&d);

    FactoryScope scope;
    DocumentTextElementProbe e{};
    CheckElementSlots(&e, kDocTextE, kOwn4567);     // Update overridden
    CheckUpdateTakesHandleAddress(&e, kDocTextE);
}

// ===========================================================================
// ENDGAME B.3.9 block UI-B (B39_RULES D1, rule B9-16): the concrete classes —
// slot order by PMF on the real header, the real ctor's retail stores, both
// retired slot-0 arms (the leaves' dtors are the recording markers above).
// ===========================================================================
UNIT_TEST(b39_uib_c5_slot_order_by_pmf) {
    B39CheckDescriptionPmf<UI::GraphDescription>();
    B39CheckElementPmf<UI::GraphElement>();
    B39CheckDescriptionPmf<UI::DocumentTextDescription>();
    B39CheckElementPmf<UI::DocumentTextElement>();
}

UNIT_TEST(b39_uib_c5_graph_ctor_stores) {
    FactoryScope scope;
    // ??0GraphDescription 0x1800d1bd0 tail.
    alignas(8) uint8_t d[0x88];
    std::memset(d, 0xCD, sizeof d);
    auto* gd = ::new (static_cast<void*>(d)) UI::GraphDescription();
    CHECK_EQ(gd->_width, 100);
    CHECK_EQ(gd->_height, 100);
    CHECK_EQ(gd->_tickSize, 4);
    CHECK_EQ(gd->_lineColor, 0xffffffffu);
    CHECK(gd->_spriteSheet == nullptr);
    CHECK(gd->_spriteName._data == nullptr);
    CHECK_EQ(gd->_id, -1);
    gd->~GraphDescription();

    // ??0GraphElement 0x1800d1880; +0xec written by nothing.
    alignas(8) uint8_t e[0x100];
    std::memset(e, 0xCD, sizeof e);
    auto* ge = ::new (static_cast<void*>(e)) UI::GraphElement();
    CHECK_EQ(ge->_width, 100);
    CHECK_EQ(ge->_height, 100);
    CHECK_EQ(ge->_tickSize, 4);
    CHECK_EQ(ge->_lineColor, -1);
    CHECK(ge->_lineSprite.spriteSheet == nullptr);
    CHECK_EQ(ge->_lineSprite.spriteIndex, -1);
    CHECK(ge->_yRange == 1.0f);
    CHECK_EQ(ge->_xDivisions, 12);
    CHECK_EQ(ge->_yDivisions, 4);
    CHECK_EQ((int)e[0xec], 0xCD);
    CHECK(ge->_seriesData == nullptr);
    CHECK_EQ(ge->_seriesCount, 0);
    CHECK_EQ(ge->_seriesCapacity, 0);
    ge->~GraphElement();
}

UNIT_TEST(b39_uib_c5_documenttext_ctor_stores) {
    FactoryScope scope;
    // ??0DocumentTextDescription 0x1800cfc30; +0xac written by nothing.
    alignas(8) uint8_t d[0xe8];
    std::memset(d, 0xCD, sizeof d);
    auto* dd = ::new (static_cast<void*>(d)) UI::DocumentTextDescription();
    CHECK(dd->_documentStringTable == nullptr);
    CHECK(dd->_frameNames._data == nullptr);
    CHECK_EQ(dd->_frameNames._size, 0);
    CHECK_EQ(dd->_linkColor, 0xffeeee00u);
    CHECK_EQ(dd->_linkHoverColor, 0xffffff00u);
    CHECK_EQ(dd->_linkPressedColor, 0xffcccc00u);
    CHECK_EQ(dd->_tabWidth, 4);
    CHECK_EQ(dd->_wrapWidth, 0x12c);
    CHECK_EQ((int)d[0xac], 0xCD);
    CHECK(dd->_colors._data == nullptr);
    CHECK(dd->_resourceList == nullptr);
    dd->~DocumentTextDescription();

    // ??0DocumentTextElement 0x1800cf410: nothing in +0x170..+0x197 and
    // +0x1b0..+0x1c7; +0x1a0 is ONE qword -1.
    alignas(8) uint8_t e[0x1c8];
    std::memset(e, 0xCD, sizeof e);
    auto* de = ::new (static_cast<void*>(e)) UI::DocumentTextElement();
    CHECK(de->_text.table == nullptr);
    CHECK_EQ(de->_text.index, -1);
    CHECK(de->_spriteSheet == nullptr);
    CHECK(de->_underlineSprite.spriteSheet == nullptr);
    CHECK_EQ(de->_underlineSprite.spriteIndex, -1);
    CHECK_EQ(de->_linkColor, 0xffeeee00u);
    CHECK_EQ(de->_tabWidth, 4);
    CHECK_EQ(de->_wrapWidth, 0x12c);
    CHECK(de->_items._data == nullptr);
    CHECK(de->_links._data == nullptr);
    CHECK(de->_sourceText._data == nullptr);
    CHECK(de->_openLink.text._data == nullptr);
    CHECK_EQ(de->_openLink.firstItem, -1);
    CHECK_EQ(de->_openLink.lastItem, -1);
    CHECK_EQ((int)de->_openLink.armed, 0);
    for (int off = 0x170; off < 0x198; ++off) CHECK_EQ((int)e[off], 0xCD);
    for (int off = 0x1b0; off < 0x1c8; ++off) CHECK_EQ((int)e[off], 0xCD);
    CHECK_EQ((int)e[0x10c], 0xCD);
    de->~DocumentTextElement();
}

UNIT_TEST(b39_uib_c5_deletingdtor_arms) {
    FactoryScope scope;
    GraphDescriptionProbe gdp;
    B39CheckDestructorArms<
UI::GraphDescription, 0x88>(VTableOf(&gdp));
    GraphElementProbe gep{};
    B39CheckDestructorArms<
UI::GraphElement, 0x100>(VTableOf(&gep));
    DocumentTextDescriptionProbe ddp;
    B39CheckDestructorArms<
UI::DocumentTextDescription, 0xe8>(VTableOf(&ddp));
    DocumentTextElementProbe dep{};
    B39CheckDestructorArms<
UI::DocumentTextElement, 0x1c8>(VTableOf(&dep));
}

// ---- Progress / Ribbon (ENDGAME B.3.9 UI-B) --------------------------------
UNIT_TEST(b39_uib_c5_progress_ribbon_pmf_and_ctors) {
    B39CheckDescriptionPmf<UI::ProgressDescription>();
    B39CheckElementPmf<UI::ProgressElement>();
    B39CheckDescriptionPmf<UI::RibbonDescription>();
    B39CheckElementPmf<UI::RibbonElement>();

    FactoryScope scope;
    // ??0ProgressDescription 0x18011a210: +0x68 ONE dword, +0x6c untouched.
    alignas(8) uint8_t pd[0xb0];
    std::memset(pd, 0xCD, sizeof pd);
    auto* p = ::new (static_cast<void*>(pd)) UI::ProgressDescription();
    CHECK_EQ(p->_backgroundColor, 0u);
    CHECK_EQ((int)pd[0x6c], 0xCD);
    CHECK(p->_spriteSheet == nullptr);
    CHECK(p->_fillLeftName._data == nullptr);
    CHECK(p->_trackRightName._data == nullptr);
    CHECK(p->_childDescription == nullptr);
    p->~ProgressDescription();
    // ??0ProgressElement 0x180119c40: six {null, -1} sprites, the three tail
    // stores, +0x12c untouched.
    alignas(8) uint8_t pe[0x130];
    std::memset(pe, 0xCD, sizeof pe);
    auto* e = ::new (static_cast<void*>(pe)) UI::ProgressElement();
    CHECK(e->_fillLeft.spriteSheet == nullptr);
    CHECK_EQ(e->_fillLeft.spriteIndex, -1);
    CHECK_EQ(e->_trackRight.spriteIndex, -1);
    CHECK_EQ(e->_backgroundColor, 0xffffffffu);
    CHECK(e->_maxValue == 1.0f);
    CHECK(e->_currentValue == 0.25f);
    CHECK_EQ((int)pe[0x12c], 0xCD);
    e->~ProgressElement();

    // ??0RibbonDescription 0x1800d5bf0: +0x6a/+0x6b never written.
    alignas(8) uint8_t rd[0x80];
    std::memset(rd, 0xCD, sizeof rd);
    auto* r = ::new (static_cast<void*>(rd)) UI::RibbonDescription();
    CHECK_EQ((int)r->_vertical, 0);
    CHECK_EQ((int)r->_uniform, 0);
    CHECK_EQ((int)rd[0x6a], 0xCD);
    CHECK_EQ((int)rd[0x6b], 0xCD);
    CHECK_EQ(r->_spacing, 0);
    CHECK(r->_itemsData == nullptr);
    CHECK_EQ(r->_itemsCount, 0);
    r->~RibbonDescription();
    // ??0RibbonElement 0x1800d5d10: +0xc2/+0xc3 never written.
    alignas(8) uint8_t re[0xd8];
    std::memset(re, 0xCD, sizeof re);
    auto* q = ::new (static_cast<void*>(re)) UI::RibbonElement();
    CHECK_EQ((int)q->_vertical, 0);
    CHECK_EQ((int)q->_uniform, 0);
    CHECK_EQ((int)re[0xc2], 0xCD);
    CHECK_EQ(q->_spacing, 0);
    CHECK(q->_extentsData == nullptr);
    CHECK_EQ(q->_extentsCapacity, 0);
    q->~RibbonElement();
}

UNIT_TEST(b39_uib_c5_progress_ribbon_deletingdtor_arms) {
    FactoryScope scope;
    ProgressDescriptionProbe pdp;
    B39CheckDestructorArms<
UI::ProgressDescription, 0xb0>(VTableOf(&pdp));
    ProgressElementProbe pep{};
    B39CheckDestructorArms<
UI::ProgressElement, 0x130>(VTableOf(&pep));
    RibbonDescriptionProbe rdp;
    B39CheckDestructorArms<
UI::RibbonDescription, 0x80>(VTableOf(&rdp));
    RibbonElementProbe rep{};
    B39CheckDestructorArms<
UI::RibbonElement, 0xd8>(VTableOf(&rep));
}

// ---- Slider (ENDGAME B.3.9 UI-B) -------------------------------------------
UNIT_TEST(b39_uib_c5_slider) {
    B39CheckDescriptionPmf<UI::SliderDescription>();
    B39CheckElementPmf<UI::SliderElement>();

    FactoryScope scope;
    // ??0SliderDescription 0x1800d7b60: +0xb4 written by nothing.
    alignas(8) uint8_t d[0xb8];
    std::memset(d, 0xCD, sizeof d);
    auto* sd = ::new (static_cast<void*>(d)) UI::SliderDescription();
    CHECK(sd->_childDescription == nullptr);
    CHECK(sd->_thumbHotName._data == nullptr);
    CHECK_EQ(sd->_trackOffsetY, 0);
    CHECK_EQ(sd->_thumbMargin, 0);
    CHECK(sd->_minimum == 0.0f);
    CHECK(sd->_maximum == 1.0f);
    CHECK_EQ(sd->_steps, 10);
    CHECK_EQ((int)d[0xb4], 0xCD);
    sd->~SliderDescription();
    // ??0SliderElement 0x1800d7570: +0x125..+0x127 and +0x12c NOT written.
    alignas(8) uint8_t e[0x130];
    std::memset(e, 0xCD, sizeof e);
    auto* se = ::new (static_cast<void*>(e)) UI::SliderElement();
    CHECK(se->_thumb.spriteSheet == nullptr);
    CHECK_EQ(se->_thumbHot.spriteIndex, -1);
    CHECK(se->_maximum == 1.0f);
    CHECK_EQ(se->_steps, 10);
    CHECK_EQ((int)se->_hovered, 0);
    CHECK(se->_value == 0.25f);
    CHECK_EQ((int)e[0x125], 0xCD);
    CHECK_EQ((int)e[0x12c], 0xCD);
    se->~SliderElement();

    SliderDescriptionProbe dp;
    B39CheckDestructorArms<
UI::SliderDescription, 0xb8>(VTableOf(&dp));
    SliderElementProbe ep{};
    B39CheckDestructorArms<
UI::SliderElement, 0x130>(VTableOf(&ep));
}

// ---- Spinner / Tab (ENDGAME B.3.9 UI-B) ------------------------------------
UNIT_TEST(b39_uib_c5_spinner_tab) {
    B39CheckDescriptionPmf<UI::SpinnerDescription>();
    B39CheckElementPmf<UI::SpinnerElement>();
    B39CheckDescriptionPmf<UI::TabDescription>();
    B39CheckElementPmf<UI::TabElement>();

    FactoryScope scope;
    // ??0SpinnerDescription 0x1800d89c0: +0x84 not written.
    alignas(8) uint8_t sd[0x88];
    std::memset(sd, 0xCD, sizeof sd);
    auto* s = ::new (static_cast<void*>(sd)) UI::SpinnerDescription();
    CHECK(s->_editDescription == nullptr);
    CHECK(s->_upDescription == nullptr);
    CHECK(s->_downDescription == nullptr);
    CHECK_EQ(s->_increment, 1);
    CHECK_EQ((int)sd[0x84], 0xCD);
    s->~SpinnerDescription();
    // ??0SpinnerElement 0x1800d8670: +0xc4 not written.
    alignas(8) uint8_t se[0xc8];
    std::memset(se, 0xCD, sizeof se);
    auto* sp = ::new (static_cast<void*>(se)) UI::SpinnerElement();
    CHECK_EQ(sp->_increment, 1);
    CHECK_EQ((int)se[0xc4], 0xCD);
    sp->~SpinnerElement();

    // ??0TabDescription 0x1800daa00: +0xa0 (_gap) and +0xa4 NOT written.
    alignas(8) uint8_t td[0xb8];
    std::memset(td, 0xCD, sizeof td);
    auto* t = ::new (static_cast<void*>(td)) UI::TabDescription();
    CHECK(t->_stripBackdropDescription == nullptr);
    CHECK(t->_spriteSheet == nullptr);
    CHECK(t->_edgeRightName._data == nullptr);
    CHECK_EQ((int)td[0xa0], 0xCD);
    CHECK_EQ((int)td[0xa4], 0xCD);
    CHECK(t->_tabsData == nullptr);
    CHECK_EQ(t->_tabsCount, 0);
    t->~TabDescription();
    // ??0TabElement 0x1800da390: +0x104 not written.
    alignas(8) uint8_t te[0x108];
    std::memset(te, 0xCD, sizeof te);
    auto* tb = ::new (static_cast<void*>(te)) UI::TabElement();
    CHECK_EQ(tb->_tabCount, 0);
    CHECK_EQ(tb->_gap, 0);
    CHECK(tb->_edgeLeft.spriteSheet == nullptr);
    CHECK_EQ(tb->_edgeRight.spriteIndex, -1);
    CHECK_EQ(tb->_activeTab, 0);
    CHECK_EQ(tb->_tabBarHeight, 0);
    CHECK_EQ(tb->_tabBarWidth, 0);
    CHECK_EQ((int)te[0x104], 0xCD);
    tb->~TabElement();

    SpinnerDescriptionProbe sdp;
    B39CheckDestructorArms<
UI::SpinnerDescription, 0x88>(VTableOf(&sdp));
    SpinnerElementProbe sep{};
    B39CheckDestructorArms<
UI::SpinnerElement, 0xc8>(VTableOf(&sep));
    TabDescriptionProbe tdp;
    B39CheckDestructorArms<
UI::TabDescription, 0xb8>(VTableOf(&tdp));
    TabElementProbe tep{};
    B39CheckDestructorArms<
UI::TabElement, 0x108>(VTableOf(&tep));
}
