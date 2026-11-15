// ============================================================================
// tests/unit/ui_element_abi_test.cpp — ENDGAME B.3 step 6, block A
//   `UI::Element` and `UI::ElementDescription` as real C++ classes
//
//   UI::Element             : Object::Internal, abstract, 0xC0, 12 slots
//   UI::Element::UpdateState  value type, 0x30
//   UI::ElementDescription  : Object::External, abstract, 0x68,  9 slots
//
// Built to the contract of graphics_material_abi_test.cpp / graphics_geometry_
// abi_test.cpp; only what differs is spelled out.
//
// PART A — LAYOUT. Alignments, where each class's own fields begin, the shape
//   traits and UpdateState's constructor stores, measured; the sizes and the
//   other offsets are the headers' `static_assert`s.
//
// PART B — SLOT ORDER. Probe subclasses of the REAL classes. Their GCC tables
//   are read back by BYTE OFFSET and each slot raw-called the way a
//   hand-inlined `(*(void***)p)[N]` caller does; the marker that runs
//   names the member at that index. `UI::Element`'s own out-of-line virtuals
//   are defined AS MARKERS at the bottom of this file — element.cpp,
//   element_vtable.cpp and elementdescription_serialize.cpp are not linked into
//   the unit host — which is also what makes GCC emit both classes' vtables here.
//
// PART C — THE ABI OF THE THREE CLASS-VALUED SLOTS.
//   * Element slot 11 `GetBorder` and ElementDescription slot 8
//     `CreateInstance` return `InternalPtr<Element>` through a hidden slot
//     (RULE B6-2): a retail-order raw caller `(this, ret, …)` must get its slot
//     written and handed back, with `this` untouched.
//   * Element slot 7 `Update` takes `InternalPtr<DialogInstance>` BY VALUE in
//     the retail source, and retail passes a POINTER to the caller's temporary
//     in R9 (`LEA R9,[RBP+0x50]` at 0x1801162ca). GCC passes a trivially
//     copyable 8-byte class by value, so the member is declared with the
//     pointer; the test raw-calls it the retail way and checks the handle the
//     member saw.
//
// WHAT IS NOT PROVEN HERE: nothing about MSVC, and nothing about the ported
// bodies (element.cpp / element_vtable.cpp / elementdescription_serialize.cpp),
// which are verified by the real run.
// ============================================================================
#include "unit.h"
#include "slot_check.h"
#include "dtor_check.h"

#include "ui/Element.h"
#include "ui/ElementDescription.h"
#include "ui/DialogInstance.h"   // B4 S4 RT2: InternalPtr<DialogInstance>::Get() needs the class complete

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

// The two unit-only seams src/engine/object/internal_class.cpp defines under
// -DBANISHED_UNIT_TEST: `Object::Internal`'s ctor registers in a factory.
namespace Object {
namespace detail {
extern InternalFactory* g_unitInternalFactory;
extern uint32_t (*g_unitInternalRandHook)(System::Random*);
}  // namespace detail
}  // namespace Object

namespace {

// ---------------------------------------------------------------------------
// Observation channel.
// ---------------------------------------------------------------------------
const void* g_lastSelf = nullptr;
int         g_lastMark = -1;
long long   g_lastArg  = 0;

void Note(const void* self, int mark, long long arg = 0) {
    g_lastSelf = self; g_lastMark = mark; g_lastArg = arg;
}
void Reset() { g_lastSelf = nullptr; g_lastMark = -1; g_lastArg = 0; }

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
const void* const* VTableOf(const void* obj) {
    return *reinterpret_cast<const void* const* const*>(obj);
}

uint32_t RandHook(System::Random*) { return 7u; }

// `Object::Internal::Internal()` registers the object in
// `InternalFactory::Get()`; the unit build answers a factory this scope owns.
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

// The twelve retail byte offsets of `??_7Element@UI@@6B@` (B36_SLOTS.md).
enum : int {
    kE0_Dtor          = 0x00, kE1_TypeName = 0x08, kE2_Type = 0x10,
    kE3_Persist       = 0x18, kE4_ComputeSize = 0x20, kE5_ComputeLayout = 0x28,
    kE6_Draw          = 0x30, kE7_Update = 0x38, kE8_Enable = 0x40,
    kE9_Contains      = 0x48, kE10_SetColor = 0x50, kE11_GetBorder = 0x58,
};
// The nine of `??_7ElementDescription@UI@@6B@`.
enum : int {
    kD0_Dtor         = 0x00, kD1_TypeName = 0x08, kD2_Type = 0x10,
    kD3_GetFileVersion = 0x18, kD4_Compile = 0x20, kD5_Serialize = 0x28,
    kD6_Reconstruct = 0x30, kD7_Destruct = 0x38, kD8_CreateInstance = 0x40,
};

// ---------------------------------------------------------------------------
// Probes. Each overrides only what `UI::Element` / `UI::ElementDescription`
// leave pure, so every other slot is the BASE member's marker below. B3X W1:
// retail slot 0 is the destructor pair; the probe's destructor records mark 0
// and a recording class-scope operator delete stands for flags 1's MemFree.
// ---------------------------------------------------------------------------
struct ElementProbe final : UI::Element {
    ~ElementProbe() override { Note(this, kE0_Dtor / 8); }
    DTOR_CHECK_RECORDING_DELETE
    void ComputeSize() override { Note(this, kE4_ComputeSize / 8); }
};

struct DescriptionProbe final : UI::ElementDescription {
    ~DescriptionProbe() override { Note(this, kD0_Dtor / 8); }
    DTOR_CHECK_RECORDING_DELETE
    Object::External* Reconstruct() override { Note(this, kD6_Reconstruct / 8); return this; }
    void Destruct() override { Note(this, kD7_Destruct / 8); }
    Object::InternalPtr<UI::Element>* CreateInstance(
            Object::InternalPtr<UI::Element>* ret, const UI::Dialog* dialog) const override {
        Note(this, kD8_CreateInstance / 8, reinterpret_cast<intptr_t>(dialog));
        new (static_cast<void*>(ret)) Object::InternalPtr<UI::Element>(
            reinterpret_cast<UI::Element*>(static_cast<uintptr_t>(0xE1E1E1E1)));
        return ret;
    }
};

// A bare External leaf: whatever it carries at +0x20 is `Object::External::
// Compile`, and ElementDescription must carry the SAME pointer (inherited).
struct BareExternalProbe final : Object::External {
    int32_t GetFileVersion() const override { return 0; }
    bool Serialize(IO::Stream&) override { return true; }
    Object::External* Reconstruct() override { return this; }
    void Destruct() override {}
};

}  // namespace

// ===========================================================================
// The out-of-line members, AS MARKERS. The real bodies are in
// src/engine/ui/{element.cpp, element_vtable.cpp, elementdescription_
// serialize.cpp} and dialog.cpp, none of which links here.
// ===========================================================================
namespace UI {
void Element::Persist(IO::Stream& stream) { Note(this, kE3_Persist / 8, reinterpret_cast<intptr_t>(&stream)); }
void Element::ComputeLayout(int32_t x, int32_t, int32_t, int32_t h) {
    Note(this, kE5_ComputeLayout / 8, (long long)x * 1000 + h);
}
void Element::Draw(SpriteContext&, uint8_t alpha) const { Note(this, kE6_Draw / 8, alpha); }
void Element::Update(const System::Time&, UserInterface*,
                     Object::InternalPtr<DialogInstance>* dialog, const UpdateState& state) {
    // Record the HANDLE the member saw through the pointer, plus mouseX, so
    // the test can tell a pointer-to-temporary from a by-value handle.
    Note(this, kE7_Update / 8,
         static_cast<long long>(reinterpret_cast<intptr_t>(dialog->Get())) + state.mouseX);
}
void Element::Enable(bool enable) { Note(this, kE8_Enable / 8, enable ? 1 : 0); }
bool Element::Contains(int32_t x, int32_t y, UserInterface*) const {
    Note(this, kE9_Contains / 8, (long long)x * 1000 + y); return true;
}
void Element::SetColor(uint32_t color) { Note(this, kE10_SetColor / 8, color); _color = color; }
Object::InternalPtr<Element>* Element::GetBorder(Object::InternalPtr<Element>* ret) const {
    Note(this, kE11_GetBorder / 8);
    new (static_cast<void*>(ret)) Object::InternalPtr<Element>(static_cast<Element*>(_border));
    return ret;
}
// (dtor inline since ENDGAME B.3.8 block RB; markers in rb_ui_abi_test.cpp)

int32_t ElementDescription::GetFileVersion() const { Note(this, kD3_GetFileVersion / 8); return 4; }
bool ElementDescription::Serialize(IO::Stream& stream) {
    Note(this, kD5_Serialize / 8, reinterpret_cast<intptr_t>(&stream)); return true;
}
// (dtor inline since ENDGAME B.3.8 block RB; markers in rb_ui_abi_test.cpp)
}  // namespace UI

// ENDGAME B.3.9 TI-UI: the two records (literals as src/engine/ui passes them).
// Element's record carries Object::Internal's in-place factory
// (DECLARE_TYPE_INHERITED_INPLACE); the factories are never called here.
::Object::Base* UI::Element::CreateNewObject() { return nullptr; }
DEFINE_TYPE(UI::Element, "Object::Internal", nullptr)
::Object::Base* UI::ElementDescription::CreateNewObject() { return nullptr; }
::Object::Base* UI::ElementDescription::CreateInPlaceObject(void*) { return nullptr; }
DEFINE_TYPE(UI::ElementDescription, "Object::External", nullptr)

// ===========================================================================
// PART A — LAYOUT
// ===========================================================================
UNIT_TEST(ui_element_sizes_match_retail) {
    // The sizes (`MOV EDX,imm` in each class's own slot-0 vector deleting
    // destructor, B36_SLOTS.md section 1) are the headers' `static_assert`s.
    CHECK_EQ(alignof(UI::Element), (size_t)8);
    CHECK_EQ(alignof(UI::ElementDescription), (size_t)8);
    // The derived fields begin at exactly sizeof(base): no tail reuse, no
    // second vptr.
    CHECK_EQ(offsetof(UI::Element, _name), sizeof(Object::Internal));
    CHECK_EQ(offsetof(UI::ElementDescription, _x), sizeof(Object::External));
    // B3X W1: a public virtual destructor (retail slot 0 = D1 + D0).
    CHECK(std::has_virtual_destructor<UI::Element>::value);
    CHECK(std::has_virtual_destructor<UI::ElementDescription>::value);
    CHECK(std::is_abstract<UI::Element>::value);
    CHECK(std::is_abstract<UI::ElementDescription>::value);
    CHECK((std::is_base_of<Object::Internal, UI::Element>::value));
    CHECK((std::is_base_of<Object::External, UI::ElementDescription>::value));
}

UNIT_TEST(ui_element_updatestate_is_retails) {
    CHECK_EQ(alignof(UI::Element::UpdateState), (size_t)8);
    CHECK(std::is_trivially_copyable<UI::Element::UpdateState>::value);

    // `??0UpdateState` (0x180115600) writes +0x00..+0x09, +0x0c..+0x13,
    // +0x18..+0x1f (to -1) and +0x20, and NOTHING else. Pre-fill, construct,
    // and check both halves of that sentence.
    alignas(8) unsigned char buf[sizeof(UI::Element::UpdateState)];
    std::memset(buf, 0xAB, sizeof buf);
    new (buf) UI::Element::UpdateState();
    for (int i = 0x00; i < 0x0a; ++i) CHECK_EQ((int)buf[i], 0);
    for (int i = 0x0a; i < 0x0c; ++i) CHECK_EQ((int)buf[i], 0xAB);
    for (int i = 0x0c; i < 0x14; ++i) CHECK_EQ((int)buf[i], 0);
    for (int i = 0x14; i < 0x18; ++i) CHECK_EQ((int)buf[i], 0xAB);   // wheel
    for (int i = 0x18; i < 0x20; ++i) CHECK_EQ((int)buf[i], 0xFF);   // mouse -1,-1
    CHECK_EQ((int)buf[0x20], 0);
    for (int i = 0x21; i < 0x30; ++i) CHECK_EQ((int)buf[i], 0xAB);
}

// ===========================================================================
// PART B — SLOT ORDER
// ===========================================================================
namespace {
// Retail slot 0 is the destructor pair (B3X W1), read raw: compiler [0] D1
// runs ~Probe once and frees nothing (flags 0), on a placement-new'd probe
// that is never destroyed again (a stack probe would be destroyed twice); [1]
// D0 runs it and hands the block to the recording operator delete (flags 1).
template <typename Probe>
void CheckProbeDtorPair() {
    alignas(Probe) unsigned char buf[sizeof(Probe)];
    Probe* p = ::new (static_cast<void*>(buf)) Probe();
    CHECK(VTableOf(p)[0] != VTableOf(p)[1]);
    Reset(); dtor_check::ResetFrees();
    dtor_check::CallD1(p);
    CHECK_EQ(g_lastMark, 0); CHECK(g_lastSelf == p);
    CHECK_EQ(dtor_check::g_frees, 0);
    Probe* h = dtor_check::NewOnMalloc<Probe>();
    Reset(); dtor_check::ResetFrees();
    dtor_check::CallD0(h);
    CHECK_EQ(g_lastMark, 0); CHECK(g_lastSelf == h);
    CHECK_EQ(dtor_check::g_frees, 1); CHECK(dtor_check::g_lastFree == h);
}
}  // namespace

UNIT_TEST(ui_element_slot_order_is_retails) {
    FactoryScope scope;
    ElementProbe probe{};
    void* raw = static_cast<void*>(&probe);

    static_assert(kE0_Dtor == 0, "retail slot 0");
    CheckProbeDtorPair<ElementProbe>();
    // Slots 1/2 (ENDGAME B.3.9 TI-UI): DECLARE_TYPE's bodies over the own record.
    CHECK(SlotAt<const char* (*)(const void*)>(raw, kE1_TypeName)(raw) == UI::Element::GetTypeInfo().name);
    CHECK_EQ(SlotAt<uint32_t (*)(const void*)>(raw, kE2_Type)(raw), UI::Element::GetTypeInfo().typeId);
    Reset(); SlotAt<void (*)(void*, void*)>(raw, kE3_Persist)(raw, (void*)0x33);
    CHECK_EQ(g_lastMark, 3); CHECK_EQ(g_lastArg, 0x33);
    Reset(); SlotAt<void (*)(void*)>(raw, kE4_ComputeSize)(raw);
    CHECK_EQ(g_lastMark, 4);
    Reset(); SlotAt<void (*)(void*, int, int, int, int)>(raw, kE5_ComputeLayout)(raw, 7, 0, 0, 9);
    CHECK_EQ(g_lastMark, 5); CHECK_EQ(g_lastArg, 7009);
    Reset(); SlotAt<void (*)(const void*, void*, uint8_t)>(raw, kE6_Draw)(raw, nullptr, 200);
    CHECK_EQ(g_lastMark, 6); CHECK_EQ(g_lastArg, 200);
    Reset(); SlotAt<void (*)(void*, bool)>(raw, kE8_Enable)(raw, true);
    CHECK_EQ(g_lastMark, 8); CHECK_EQ(g_lastArg, 1);
    Reset(); SlotAt<bool (*)(const void*, int, int, void*)>(raw, kE9_Contains)(raw, 3, 4, nullptr);
    CHECK_EQ(g_lastMark, 9); CHECK_EQ(g_lastArg, 3004);
    Reset(); SlotAt<void (*)(void*, uint32_t)>(raw, kE10_SetColor)(raw, 0x11223344u);
    CHECK_EQ(g_lastMark, 10); CHECK_EQ(probe._color, 0x11223344u);

    // Twelve distinct retail entries = thirteen compiler entries (D1 != D0).
    const void* const* vt = VTableOf(raw);
    for (int i = 0; i < 13; ++i)
        for (int j = i + 1; j < 13; ++j)
            CHECK(vt[i] != vt[j]);

    // Single inheritance, `this` unadjusted.
    CHECK(static_cast<void*>(static_cast<UI::Element*>(&probe)) == raw);
    CHECK(static_cast<void*>(static_cast<Object::Internal*>(&probe)) == raw);
    CHECK(static_cast<void*>(static_cast<Object::Base*>(&probe)) == raw);
}

UNIT_TEST(ui_elementdescription_slot_order_is_retails) {
    DescriptionProbe probe;
    void* raw = static_cast<void*>(&probe);

    static_assert(kD0_Dtor == 0, "retail slot 0");
    CheckProbeDtorPair<DescriptionProbe>();
    // Slots 1/2 (ENDGAME B.3.9 TI-UI): DECLARE_TYPE's bodies over the own record.
    CHECK(SlotAt<const char* (*)(const void*)>(raw, kD1_TypeName)(raw) == UI::ElementDescription::GetTypeInfo().name);
    CHECK_EQ(SlotAt<uint32_t (*)(const void*)>(raw, kD2_Type)(raw), UI::ElementDescription::GetTypeInfo().typeId);
    Reset(); SlotAt<int32_t (*)(const void*)>(raw, kD3_GetFileVersion)(raw);
    CHECK_EQ(g_lastMark, 3);
    Reset(); SlotAt<bool (*)(void*, void*)>(raw, kD5_Serialize)(raw, (void*)0x55);
    CHECK_EQ(g_lastMark, 5); CHECK_EQ(g_lastArg, 0x55);
    Reset(); SlotAt<void* (*)(void*)>(raw, kD6_Reconstruct)(raw);
    CHECK_EQ(g_lastMark, 6);
    Reset(); SlotAt<void (*)(void*)>(raw, kD7_Destruct)(raw);
    CHECK_EQ(g_lastMark, 7);

    // Slot 4 is `Object::External::Compile`, INHERITED (RULE B6-1.5).
    BareExternalProbe bare;
    CHECK(RetailEntry<Object::Base>(raw, kD4_Compile / 8) ==
          RetailEntry<Object::Base>(static_cast<void*>(&bare), kD4_Compile / 8));

    // Nine distinct retail entries = ten compiler entries (D1 != D0):
    // CreateInstance at slot 8, no Construct (B6-1.1).
    const void* const* vt = VTableOf(raw);
    for (int i = 0; i < 10; ++i)
        for (int j = i + 1; j < 10; ++j)
            CHECK(vt[i] != vt[j]);

    CHECK(static_cast<void*>(static_cast<Object::External*>(&probe)) == raw);
}

// ===========================================================================
// PART C — the class-valued slots, raw-called the RETAIL way
// ===========================================================================
UNIT_TEST(ui_element_slot11_getborder_carries_the_hidden_return_slot) {
    FactoryScope scope;
    ElementProbe probe{};
    probe._border = reinterpret_cast<UI::Element*>(static_cast<uintptr_t>(0xB0B0B0B0));
    void* raw = static_cast<void*>(&probe);
    const void* const* vtBefore = VTableOf(raw);

    // Retail order: RCX = this, RDX = the caller's storage, returned in RAX.
    void* slot = nullptr;
    Reset();
    void** back = SlotAt<void** (*)(const void*, void**)>(raw, kE11_GetBorder)(raw, &slot);
    CHECK_EQ(g_lastMark, 11);
    CHECK(g_lastSelf == raw);
    CHECK(back == &slot);
    CHECK(slot == probe._border);
    CHECK(VTableOf(raw) == vtBefore);   // nothing was written through `this`
}

UNIT_TEST(ui_elementdescription_slot8_createinstance_carries_the_hidden_return_slot) {
    DescriptionProbe probe;
    void* raw = static_cast<void*>(&probe);
    const void* const* vtBefore = VTableOf(raw);

    void* slot = nullptr;
    const void* dialog = reinterpret_cast<const void*>(static_cast<uintptr_t>(0xD1A1));
    Reset();
    void** back = SlotAt<void** (*)(const void*, void**, const void*)>(
        raw, kD8_CreateInstance)(raw, &slot, dialog);
    CHECK_EQ(g_lastMark, 8);
    CHECK(g_lastSelf == raw);
    CHECK_EQ(g_lastArg, 0xD1A1);
    CHECK(back == &slot);
    CHECK(slot == reinterpret_cast<void*>(static_cast<uintptr_t>(0xE1E1E1E1)));
    CHECK(VTableOf(raw) == vtBefore);
}

UNIT_TEST(ui_element_slot7_update_receives_a_pointer_to_the_handle) {
    FactoryScope scope;
    ElementProbe probe{};
    void* raw = static_cast<void*>(&probe);

    // Retail's own call site: the handle lives in the caller's frame and R9 is
    // its ADDRESS (`LEA R9,[RBP+0x50]`, 0x1801162ca).
    void* handle = reinterpret_cast<void*>(static_cast<uintptr_t>(0x7000));
    UI::Element::UpdateState state;
    state.mouseX = 5;
    Reset();
    SlotAt<void (*)(void*, const void*, void*, void**, const void*)>(raw, kE7_Update)(
        raw, nullptr, nullptr, &handle, &state);
    CHECK_EQ(g_lastMark, 7);
    CHECK_EQ(g_lastArg, 0x7000 + 5);
}

