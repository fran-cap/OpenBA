// ============================================================================
// tests/unit/rb_ui_abi_test.cpp — ENDGAME B.3.8 block RB, the UI Runtime bases
//
// B38_RULES D1 / RULE B8-1: a Runtime base that an exe class derives from gets
// a REAL constructor whose body is `InitFields`, and a destructor whose body is
// `ReleaseFields` (the ctor / dtor doors that shared them were retired by
// ENDGAME B.3.9 UI-D). What this file pins, on the shipped headers:
//
// PART A — STAMP ORDER. A test subclass's construction runs the shared body
//   while the object carries the BASE's compiler table (the compiler stamped
//   Base -> Internal/External -> X, and not yet the subclass), and ends on the
//   subclass's table; destruction runs `ReleaseFields` with the base's table
//   restamped. That is retail's order: `CALL ??0Internal`, `MOV [RBX],??_7X`,
//   body; and on the way out `MOV [RBX],??_7X`, body, `~Internal`.
// PART B — (ENDGAME B.3.9 UI-D) the doors are retired; Element's and
//   ElementDescription's own slot 0, both arms.
// PART C — RETAIL BODIES THAT ARE INLINE, checked byte for byte: the
//   DialogInstance default ctor (0x180114ca0), DialogController's
//   (0x1800cd320, three zero stores, +0x28 untouched), DialogControllerConfig's
//   (External's three zero stores only), and DialogInstance's concrete slot 0
//   (the exe's 0x140020cb0: ~DialogInstance, MemFree on flags & 1).
//
// NOT PROVEN HERE: the real `InitFields` / `ReleaseFields` bodies. They live in
// src/engine/ui/{dialog_create.cpp, dialog.cpp, element.cpp}, which do not
// link into this host test, so they are defined
// below AS RECORDING MARKERS. The bodies were MOVED from the doors unchanged;
// the real run and the resave gate verify them.
// ============================================================================
#include "unit.h"
#include "abi.h"   // RT_ASM_LABEL

#include "ui/DialogInstance.h"
#include "ui/DialogController.h"
#include "ui/DialogControllerConfig.h"
#include "ui/Element.h"
#include "ui/ElementDescription.h"
#include "ui/SpriteSheet.h"
#include "ui/SpriteCapture.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>

#include "dtor_check.h"

namespace Object {
namespace detail {
extern InternalFactory* g_unitInternalFactory;
}  // namespace detail
}  // namespace Object

// The compiler vtables of the three bases with a shared body, by their Itanium
// names (the linker checks the spelling). The object's vptr is group[2].
extern const void* const g_rbDiVtGroup[] RT_ASM_LABEL("_ZTVN2UI14DialogInstanceE");
extern const void* const g_rbElVtGroup[] RT_ASM_LABEL("_ZTVN2UI7ElementE");
extern const void* const g_rbEdVtGroup[] RT_ASM_LABEL("_ZTVN2UI18ElementDescriptionE");
extern const void* const g_rbSsVtGroup[] RT_ASM_LABEL("_ZTVN2UI11SpriteSheetE");
extern const void* const g_rbScVtGroup[] RT_ASM_LABEL("_ZTVN2UI13SpriteCaptureE");

namespace {

const void* DiVt() { return &g_rbDiVtGroup[2]; }
const void* ElVt() { return &g_rbElVtGroup[2]; }
const void* EdVt() { return &g_rbEdVtGroup[2]; }
const void* VptrOf(const void* obj) { return *static_cast<const void* const*>(obj); }

struct FactoryScope {
    ::Object::InternalFactory  factory;
    ::Object::InternalFactory* saved;
    FactoryScope() : factory(), saved(::Object::detail::g_unitInternalFactory) {
        ::Object::detail::g_unitInternalFactory = &factory;
    }
    ~FactoryScope() { ::Object::detail::g_unitInternalFactory = saved; }
};

// What the markers saw.
struct Seen {
    int         initCalls = 0, releaseCalls = 0;
    const void* initSelf = nullptr;
    const void* initVptr = nullptr;
    const void* releaseVptr = nullptr;
    const void* args[4] = {};
    int32_t     flags = 0;
    // B3X W1: the object each ReleaseFields ran on, in call order (the
    // destructor log CHECK_ARRAY_DELETE reads).
    const void* releaseSelf[4] = {};
};
Seen g_di, g_el, g_ed;

}  // namespace

// ===========================================================================
// The shared bodies, AS MARKERS (see the banner). Each records the vptr it
// runs under and writes a recognisable pattern into fields the real body also
// writes.
// ===========================================================================
namespace UI {
void DialogInstance::InitFields(UserInterface* ui, const Dialog* dialog,
                                const Object::InternalPtr<DialogInstance>* parent,
                                Object::Base* dataSource, void* userData, int32_t userFlags) {
    ++g_di.initCalls;
    g_di.initSelf = this;
    g_di.initVptr = VptrOf(this);
    g_di.args[0] = ui; g_di.args[1] = dialog; g_di.args[2] = parent->Get();
    g_di.args[3] = dataSource;
    _ui = ui;
    _dialog = dialog;
    _parent = *parent;
    _dataSource = dataSource;
    _userData = userData;
    _userFlags = userFlags;
}
void DialogInstance::ReleaseFields() {
    if (g_di.releaseCalls < 4) g_di.releaseSelf[g_di.releaseCalls] = this;
    ++g_di.releaseCalls;
    g_di.releaseVptr = VptrOf(this);
}
void Element::InitFields() {
    ++g_el.initCalls;
    g_el.initSelf = this;
    g_el.initVptr = VptrOf(this);
    _alignment = 3;
    _color = 0xFFFFFFFFu;
}
void Element::ReleaseFields() {
    if (g_el.releaseCalls < 4) g_el.releaseSelf[g_el.releaseCalls] = this;
    ++g_el.releaseCalls;
    g_el.releaseVptr = VptrOf(this);
}
void ElementDescription::InitFields() {
    ++g_ed.initCalls;
    g_ed.initSelf = this;
    g_ed.initVptr = VptrOf(this);
    _id = -1;
    _elementType = 3;
}
// B4 Y RT-UI: ~ElementDescription's one release (+0x60) is the member
// `_textName`'s ~String; there is no ReleaseFields hook left to stand in for.
// The tests below watch that String's reference count instead.
}  // namespace UI

namespace {

// ---- test subclasses (stand-ins for the step-8 exe classes) ---------------
struct RbDialog final : UI::DialogInstance {
    RbDialog() = default;
    RbDialog(UI::UserInterface* ui, const UI::Dialog* d,
             Object::InternalPtr<UI::DialogInstance> parent, Object::Base* ds,
             void* ud, int32_t uf)
        : UI::DialogInstance(ui, d, parent, ds, ud, uf) {}
};
struct RbPanel final : UI::DialogController {
};
struct RbConfig final : UI::DialogControllerConfig {
    int32_t GetFileVersion() const override { return 0; }
    bool Serialize(IO::Stream&) override { return true; }
    Object::External* Reconstruct() override { return this; }
    void Destruct() override {}
};
struct RbElement final : UI::Element {
    void ComputeSize() override {}
};
// A shared (refcount 2) String buffer: ~ElementDescription's member ~String
// takes the SHARING arm (decrement, null the field), so one run leaves 1.
// The test hands the probe's field one reference of its own (refcount 2).
struct SharedName {
    System::String s{"name"};
    char* data() { return s._data; }
    SharedName() { ++RtSystem::StringRefCount(s._data); }
};

struct RbElementDesc final : UI::ElementDescription {
    Object::External* Reconstruct() override { return this; }
    void Destruct() override {}
    Object::InternalPtr<UI::Element>* CreateInstance(
            Object::InternalPtr<UI::Element>* ret, const UI::Dialog*) const override {
        return ret;
    }
};

template <class T>
T* BuildIn(unsigned char* buf, unsigned char fill) {
    std::memset(buf, fill, sizeof(T));
    // DEFAULT-initialisation (no `()`): value-initialisation would zero the
    // bytes first for a probe whose own ctor is defaulted, hiding the holes.
    return ::new (static_cast<void*>(buf)) T;
}
template <class T>
uint64_t Q(const T* obj, size_t off) {
    uint64_t v;
    std::memcpy(&v, reinterpret_cast<const unsigned char*>(obj) + off, sizeof v);
    return v;
}
template <class T>
uint32_t D(const T* obj, size_t off) {
    uint32_t v;
    std::memcpy(&v, reinterpret_cast<const unsigned char*>(obj) + off, sizeof v);
    return v;
}
template <class T>
uint8_t By(const T* obj, size_t off) { return reinterpret_cast<const unsigned char*>(obj)[off]; }

// ===========================================================================
// PART A — stamp order through a test subclass
// ===========================================================================
UNIT_TEST(rb_dialoginstance_ctor_runs_shared_body_under_base_table) {
    FactoryScope scope;
    g_di = Seen{};
    int dialogToken = 0, dsToken = 0, userToken = 0;
    alignas(8) unsigned char parentObj[0x10] = {};
    Object::InternalPtr<UI::DialogInstance> parent(
        reinterpret_cast<UI::DialogInstance*>(parentObj));
    alignas(8) unsigned char buf[sizeof(RbDialog)];
    std::memset(buf, 0xCD, sizeof buf);
    RbDialog* d = ::new (static_cast<void*>(buf)) RbDialog(
        reinterpret_cast<UI::UserInterface*>(&dialogToken + 1),
        reinterpret_cast<const UI::Dialog*>(&dialogToken), parent,
        reinterpret_cast<Object::Base*>(&dsToken), &userToken, 0x1234);
    CHECK_EQ(g_di.initCalls, 1);
    CHECK(g_di.initSelf == static_cast<void*>(buf));
    CHECK(g_di.initVptr == DiVt());          // base stamped, subclass not yet
    CHECK(VptrOf(d) != DiVt());              // ... and the subclass last
    CHECK(g_di.args[1] == &dialogToken);
    CHECK(g_di.args[2] == static_cast<void*>(parentObj));   // by value through the ctor
    CHECK(g_di.args[3] == &dsToken);
    CHECK_EQ(d->_userFlags, 0x1234);
    d->~RbDialog();
    CHECK_EQ(g_di.releaseCalls, 1);
    CHECK(g_di.releaseVptr == DiVt());       // ~DialogInstance restamped first
}

UNIT_TEST(rb_element_ctor_dtor_run_shared_bodies_under_base_table) {
    FactoryScope scope;
    g_el = Seen{};
    alignas(8) unsigned char buf[sizeof(RbElement)];
    RbElement* e = BuildIn<RbElement>(buf, 0xCD);
    CHECK_EQ(g_el.initCalls, 1);
    CHECK(g_el.initVptr == ElVt());
    CHECK(VptrOf(e) != ElVt());
    CHECK_EQ(e->_alignment, 3);
    e->~RbElement();
    CHECK_EQ(g_el.releaseCalls, 1);
    CHECK(g_el.releaseVptr == ElVt());
}

UNIT_TEST(rb_elementdescription_ctor_dtor_run_shared_bodies_under_base_table) {
    g_ed = Seen{};
    alignas(8) unsigned char buf[sizeof(RbElementDesc)];
    RbElementDesc* d = BuildIn<RbElementDesc>(buf, 0xCD);
    CHECK_EQ(g_ed.initCalls, 1);
    CHECK(g_ed.initVptr == EdVt());
    CHECK(VptrOf(d) != EdVt());
    // External() ran first (retail inlines its three zero stores).
    CHECK_EQ(Q(d, 0x08), (uint64_t)0);
    CHECK_EQ(Q(d, 0x10), (uint64_t)0);
    CHECK_EQ(Q(d, 0x18), (uint64_t)0);
    SharedName name;
    d->_textName._data = name.data();
    d->~RbElementDesc();
    CHECK_EQ(RtSystem::StringRefCount(name.data()), 1);   // released exactly once
    CHECK(d->_textName._data == nullptr);                  // the sharing arm
}

// ===========================================================================
// PART B — ENDGAME B.3.9 UI-D: the ctor / dtor doors of DialogInstance,
// Element and ElementDescription are retired with their minted tables, so the
// door-vs-ctor comparisons went with them. What replaces them for the two
// abstract bases: their OWN slot 0 (the retired RT_DELDTOR rows), both arms,
// through a probe that declares no destructor of its own. B3X W1: the flags-0
// arm is the QUALIFIED `~Element()` / `~ElementDescription()`; the vector arm
// (flags 2, no free: Element 0x180035c10, ElementDescription 0x180035350) has
// no Itanium counterpart, and `delete[]` of `new Probe[3]` replaces it.
// ===========================================================================
struct B39ElProbe final : UI::Element {
    void ComputeSize() override {}
};
struct B39EdProbe final : UI::ElementDescription {
    // B4 Y RT-UI: the destructor log the vector arm checks (the base's own
    // body is empty now; its member ~String is checked by the scalar arm).
    ~B39EdProbe() override {
        if (g_ed.releaseCalls < 4) g_ed.releaseSelf[g_ed.releaseCalls] = this;
        ++g_ed.releaseCalls;
    }
    Object::External* Reconstruct() override { return this; }
    void Destruct() override {}
    Object::InternalPtr<UI::Element>* CreateInstance(
            Object::InternalPtr<UI::Element>* ret, const UI::Dialog*) const override {
        return ret;
    }
};
static_assert(sizeof(B39ElProbe) == 0xc0, "the vector arm's stride is sizeof(Element)");
static_assert(sizeof(B39EdProbe) == 0x68, "the vector arm's stride is sizeof(ElementDescription)");

UNIT_TEST(b39_uid_element_slot0_both_arms) {
    FactoryScope scope;
    // scalar arm, flag 0: destruct only
    alignas(8) unsigned char one[sizeof(B39ElProbe)];
    B39ElProbe* e = ::new (static_cast<void*>(one)) B39ElProbe();
    g_el = Seen{};
#if defined(__clang__) && !defined(_MSC_VER)
    // Clang with the Itanium ABI traps in abstract-base D1; test the base D2 body.
    extern void ClangElementBaseDtor(UI::Element*) RT_ASM_LABEL("_ZN2UI7ElementD2Ev");
    ClangElementBaseDtor(e);
#else
    e->UI::Element::~Element();
#endif
    CHECK_EQ(g_el.releaseCalls, 1);
    CHECK(g_el.releaseVptr == ElVt());

    // the retired vector arm: delete[] destroys from the top down
    g_el = Seen{};
    CHECK_ARRAY_DELETE(B39ElProbe, g_el.releaseCalls, g_el.releaseSelf);
    CHECK(g_el.releaseVptr == ElVt());
}

UNIT_TEST(b39_uid_elementdescription_slot0_both_arms) {
    alignas(8) unsigned char one[sizeof(B39EdProbe)];
    B39EdProbe* d = ::new (static_cast<void*>(one)) B39EdProbe();
    g_ed = Seen{};
    SharedName name;
    d->_textName._data = name.data();
#if defined(__clang__) && !defined(_MSC_VER)
    // Clang with the Itanium ABI traps in abstract-base D1; test the base D2 body.
    extern void ClangDescriptionBaseDtor(UI::ElementDescription*) RT_ASM_LABEL("_ZN2UI18ElementDescriptionD2Ev");
    ClangDescriptionBaseDtor(d);
#else
    d->UI::ElementDescription::~ElementDescription();
#endif
    CHECK_EQ(RtSystem::StringRefCount(name.data()), 1);
    CHECK(d->_textName._data == nullptr);

    g_ed = Seen{};
    CHECK_ARRAY_DELETE(B39EdProbe, g_ed.releaseCalls, g_ed.releaseSelf);
}

// ===========================================================================
// PART C — the inline retail bodies, byte for byte
// ===========================================================================
UNIT_TEST(rb_dialoginstance_default_ctor_is_retail_0x180114ca0) {
    FactoryScope scope;
    alignas(8) unsigned char buf[sizeof(RbDialog)];
    RbDialog* d = BuildIn<RbDialog>(buf, 0xCD);
    const uint64_t kUnwritten8 = 0xCDCDCDCDCDCDCDCDull;
    CHECK_EQ(Q(d, 0x10), (uint64_t)0);          // _ui
    CHECK_EQ(Q(d, 0x18), (uint64_t)0);          // _dialog
    CHECK_EQ(Q(d, 0x20), (uint64_t)0);          // _flags + _result
    CHECK_EQ(Q(d, 0x28), (uint64_t)0);          // _parent
    CHECK_EQ(Q(d, 0x30), kUnwritten8);          // _dataSource: NOT written by 0x180114ca0
    CHECK_EQ(Q(d, 0x38), (uint64_t)0);          // _children head
    CHECK_EQ(Q(d, 0x40), (uint64_t)0);          //           tail
    CHECK_EQ(D(d, 0x48), 0u);                   //           count (DWORD)
    CHECK_EQ(D(d, 0x4c), 0xCDCDCDCDu);          // hole
    CHECK_EQ(Q(d, 0x50), (uint64_t)0);          // _root
    CHECK_EQ(Q(d, 0x58), (uint64_t)0);          // _spriteSheet
    CHECK_EQ(D(d, 0x60), 0xFFFFFFFFu);          // _spriteIndex
    CHECK_EQ(D(d, 0x64), 0xCDCDCDCDu);          // hole
    for (size_t off = 0x68; off < 0x90; off += 8)
        CHECK_EQ(Q(d, off), (uint64_t)0);       // _timers, _controllers, upper-left
    CHECK_EQ(Q(d, 0x90), ~(uint64_t)0);         // both hover ints, one QWORD -1
    CHECK_EQ((int)By(d, 0x98), 0);              // _hovering (BYTE)
    CHECK_EQ((int)By(d, 0x99), 0xCD);           // hole
    CHECK_EQ(Q(d, 0xa0), (uint64_t)0);          // _hoverTime
    CHECK_EQ(Q(d, 0xa8), (uint64_t)0);          // _userData
    CHECK_EQ(Q(d, 0xb0), kUnwritten8);          // _userFlags + hole: NOT written
    d->~RbDialog();
}

UNIT_TEST(rb_dialoginstance_slot0_is_the_exe_scalar_deleting_dtor) {
    FactoryScope scope;
    g_di = Seen{};
    // Retail 0x140020cb0: ~DialogInstance, then MemFree when flags & 1. B3X W1:
    // that is D0, compiler slot 1 (`_ZTV…[3]`), `void (*)(void*)` — read RAW on
    // purpose to pin the two-slot order; slot 0 (`[2]`) is D1, which frees
    // nothing.
    static_assert(dtor_check::HasClassDelete<UI::DialogInstance>::value, "D0 frees through ??3 (MemFree)");
    void* mem = Object::detail::InternalMemAlloc(sizeof(UI::DialogInstance), 8);
    UI::DialogInstance* d = ::new (mem) UI::DialogInstance();
    CHECK(VptrOf(d) == DiVt());
    const void* const* vt = static_cast<const void* const*>(DiVt());
    CHECK(vt[0] != vt[1]);
    using D0 = void (*)(void*);
    D0 d0 = reinterpret_cast<D0>(const_cast<void*>(vt[1]));
    d0(d);
    CHECK_EQ(g_di.releaseCalls, 1);
    CHECK(g_di.releaseVptr == DiVt());

    // The free, recorded: the same slot of a stand-in whose class-scope
    // operator delete records the block.
    UI::DialogInstance* r = dtor_check::NewOnMalloc<dtor_check::Recording<UI::DialogInstance>>();
    g_di = Seen{};
    dtor_check::ResetFrees();
    dtor_check::CallD0(r);
    CHECK_EQ(g_di.releaseCalls, 1);
    CHECK_EQ(dtor_check::g_frees, 1);
    CHECK(dtor_check::g_lastFree == r);
}

UNIT_TEST(rb_dialogcontroller_ctor_is_retail_0x1800cd320) {
    FactoryScope scope;
    alignas(8) unsigned char buf[sizeof(RbPanel)];
    RbPanel* c = BuildIn<RbPanel>(buf, 0xCD);
    CHECK_EQ(Q(c, 0x10), (uint64_t)0);                        // _config
    CHECK_EQ(Q(c, 0x18), (uint64_t)0);                        // _dialog
    CHECK_EQ(Q(c, 0x20), (uint64_t)0);                        // _container
    CHECK_EQ(Q(c, 0x28), 0xCDCDCDCDCDCDCDCDull);              // _dataSource: NOT written
    c->~RbPanel();
}

UNIT_TEST(rb_dialogcontrollerconfig_ctor_is_retail_0x18003b4a0) {
    alignas(8) unsigned char buf[sizeof(RbConfig)];
    RbConfig* c = BuildIn<RbConfig>(buf, 0xCD);
    CHECK_EQ(Q(c, 0x08), (uint64_t)0);
    CHECK_EQ(Q(c, 0x10), (uint64_t)0);
    CHECK_EQ(Q(c, 0x18), (uint64_t)0);
    c->~RbConfig();
}

UNIT_TEST(rb_ui_bases_shape) {
    CHECK(!std::is_abstract<UI::DialogInstance>::value);   // the exe instantiates it
    CHECK(!std::is_abstract<UI::DialogController>::value);      // B.3.9 UI-D: own slot 0
    CHECK(std::is_abstract<UI::DialogControllerConfig>::value);  // retail's _purecall slots
    CHECK(std::is_abstract<UI::Element>::value);
    CHECK(std::is_abstract<UI::ElementDescription>::value);
    // B3X W1: retail slot 0 = D1 + D0, a public virtual destructor.
    CHECK(std::has_virtual_destructor<UI::DialogInstance>::value);
    CHECK(std::has_virtual_destructor<UI::DialogController>::value);
    CHECK(std::has_virtual_destructor<UI::DialogControllerConfig>::value);
    CHECK(std::has_virtual_destructor<UI::Element>::value);
    CHECK(std::has_virtual_destructor<UI::ElementDescription>::value);
}

// ===========================================================================
// PART D — the two UI resources the exe instantiates itself (B38_SLOTS
// section 4: SpriteSheet 0x14015c3b0, SpriteCapture 0x14015d550). Their
// ctors are inline (the REAL retail bodies, 0x18011d600 / 0x1800d9140);
// their virtuals and the out-of-line dtors are ui_resources_abi_test.cpp's
// markers. Slot 0 is the exe scalar deleting dtor (0x1400121e0 / 0x140013f80).
// ===========================================================================
UNIT_TEST(rb_spritesheet_ctor_is_retail_0x18011d600_and_concrete) {
    alignas(8) unsigned char buf[sizeof(UI::SpriteSheet)];
    std::memset(buf, 0xCD, sizeof buf);
    UI::SpriteSheet* s = ::new (static_cast<void*>(buf)) UI::SpriteSheet();
    CHECK(VptrOf(s) == &g_rbSsVtGroup[2]);
    for (size_t off = 0x08; off < 0x50; off += 8) CHECK_EQ(Q(s, off), (uint64_t)0);
    CHECK_EQ((int)By(s, 0x50), 0);
    CHECK_EQ((int)By(s, 0x51), 0xCD);            // never written
    // ENDGAME B.3.9 UI-D: the DoorBody bridge is retired with the ctor door;
    // the real ctor is the only body.
    s->~SpriteSheet();                            // destruct only (stack buffer)

    // B3X W1: retail slot 0 with flags 1 is D0, `_ZTV…[3]` (`[2]` is D1, which
    // frees nothing) — read RAW on purpose to pin the two-slot order.
    CHECK(g_rbSsVtGroup[2] != g_rbSsVtGroup[3]);
    void* mem = Object::detail::InternalMemAlloc(sizeof(UI::SpriteSheet), 8);
    UI::SpriteSheet* h = ::new (mem) UI::SpriteSheet();
    using D0 = void (*)(void*);
    reinterpret_cast<D0>(const_cast<void*>(g_rbSsVtGroup[3]))(h);
    // The free, recorded on a stand-in with a recording class operator delete.
    UI::SpriteSheet* r = dtor_check::NewOnMalloc<dtor_check::Recording<UI::SpriteSheet>>();
    dtor_check::ResetFrees();
    dtor_check::CallD0(r);
    CHECK_EQ(dtor_check::g_frees, 1);
    CHECK(dtor_check::g_lastFree == r);
}

UNIT_TEST(rb_spritecapture_ctor_is_retail_0x1800d9140_and_concrete) {
    alignas(8) unsigned char buf[sizeof(UI::SpriteCapture)];
    std::memset(buf, 0xCD, sizeof buf);
    UI::SpriteCapture* c = ::new (static_cast<void*>(buf)) UI::SpriteCapture();
    CHECK(VptrOf(c) == &g_rbScVtGroup[2]);
    for (size_t off = 0x08; off < 0x58; off += 8) CHECK_EQ(Q(c, off), (uint64_t)0);
    // ENDGAME B.3.9 UI-D: the DoorBody bridge is retired with the ctor door;
    // the real ctor is the only body.
    c->~SpriteCapture();

    // B3X W1: flags 1 is D0, `_ZTV…[3]`, read RAW on purpose (see above).
    CHECK(g_rbScVtGroup[2] != g_rbScVtGroup[3]);
    void* mem = Object::detail::InternalMemAlloc(sizeof(UI::SpriteCapture), 8);
    UI::SpriteCapture* h = ::new (mem) UI::SpriteCapture();
    using D0 = void (*)(void*);
    reinterpret_cast<D0>(const_cast<void*>(g_rbScVtGroup[3]))(h);
    UI::SpriteCapture* r = dtor_check::NewOnMalloc<dtor_check::Recording<UI::SpriteCapture>>();
    dtor_check::ResetFrees();
    dtor_check::CallD0(r);
    CHECK_EQ(dtor_check::g_frees, 1);
    CHECK(dtor_check::g_lastFree == r);
}

}  // namespace
