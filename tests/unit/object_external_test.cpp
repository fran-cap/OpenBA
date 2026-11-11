// ============================================================================
// tests/unit/object_external_test.cpp — ENDGAME B.3.3 step 5 (EXTERNAL)
// ============================================================================
//
// What `Object::External` claims, checked by the compiler and by a running test
// rather than by a comment:
//
//   1. LAYOUT — the real classes and the `RtObject::*` alias PODs describe the
//      same bytes, field for field. The binding asserts live in External.h and
//      fire at compile time; the ones restated here are so a reader of THIS
//      file sees the claim rather than having to trust the header.
//   2. SLOT ORDER — External's eight slots, reached the way every unconverted C
//      body in the tree still reaches them: `(*(void***)p)[N](p, …)`, applied
//      to the list measured at `??_7External@Object@@6B@`
//      (0x1801547a0) on 2026-09-18.
//   3. `Type()` / `TypeName()` through Base -> External -> a derived leaf.
//   4. `Object::Destroy(p)` reaching retail slot 0 with flags 1 — B3X W1:
//      `delete p`, the compiler's D0 (compiler slot 1), which runs the chain
//      and calls the class-scope operator delete; retail's flags 0 is D1
//      (compiler slot 0), the chain with no free. (Every MSVC `??_G` answered
//      `this`; the Itanium pair returns nothing.)
//   5. THE TWO NODE LAYOUTS — `Object::detail::ResourceFind` (0x18-wide
//      element, node tail at parent +0x18 / left +0x20 / right +0x28) and
//      `ReferenceFind` (0x10-wide, +0x10 / +0x18 / +0x20), each driven with
//      seven keys at the ROOT and at TWO LEVELS DOWN.
//      reference_one_bst_find_two_node_layouts.md: a find written against the
//      wrong layout answers correctly for whatever key happens to be the root
//      and is permanently wrong below it, so a lookup that worked once proves
//      nothing. Both trees are built by hand — no allocator, no registry.
//
// WHAT IS NOT TESTED HERE, AND WHY. Every member of `Object::ExternalFactory`
// and slots 5-8 of `Object::ExternalList` are doors into the port registry
// (src/engine/object/external_class_doors.cpp), which the unit build cannot link;
// those are covered by the real run. Nothing here constructs a PRODUCTION
// `External` either: the classes have no live caller until step 6's caller
// batches move them, and every live `External` in the process still wears the
// MINTED vtable.
// ============================================================================

#include "unit.h"
#include "slot_check.h"
#include "dtor_check.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

#include "object/External.h"

namespace {

int g_destructCount  = 0;
int g_serializeCount = 0;
int g_dtorCount      = 0;

// A concrete External. Retail's is ABSTRACT — slots 3, 5, 6 and 7 are
// `_purecall` at 0x18007ec28 — so a test needs a leaf, exactly as every real
// resource type is one. Slot 4 (`Compile`) is deliberately NOT overridden: the
// raw call through the vtable must reach `External`'s own `MOV EAX,1; RET`.
class TestExternal : public Object::External {
public:
    int32_t           GetFileVersion() const override { return 7; }
    bool              Serialize(IO::Stream&) override { ++g_serializeCount; return true; }
    Object::External* Reconstruct() override { return this; }
    void              Destruct() override { ++g_destructCount; }

    uint32_t _marker = 0xABCDEF01u;   // a field past External's 0x20 bytes

    // B3X W1: records its destructor (retail slot 0), and frees through a
    // recording class-scope operator delete with the allocator the test block
    // comes from (Object::TypeTreeAlloc / TypeTreeFree, malloc/free here).
    ~TestExternal() override { ++g_dtorCount; }
    static void operator delete(void* p) { dtor_check::NoteFree(p); Object::TypeTreeFree(p); }
};

void** VTableOf(const void* o) { return *reinterpret_cast<void** const*>(o); }

}  // namespace

// ---------------------------------------------------------------------------
// 2. Slot order, read the way an unconverted C body reads it
// ---------------------------------------------------------------------------
UNIT_TEST(object_external_vtable_slot_order_is_retails) {
    TestExternal obj;
    void*  self = &obj;
    void** vt   = VTableOf(self);

    // slot 1 — TypeName (0x1800066c0, `MOV RAX,qword[0x1801f9b40]`). The name
    // is CONTENT-only and is never compared by pointer (B33_RULES §2.4).
    using TypeNameFn = const char* (*)(const void*);
    const char* name = reinterpret_cast<TypeNameFn>(vt[RetailSlot<TestExternal>(1)])(self);
    CHECK(name != nullptr);
    CHECK_STREQ(name, "External");

    // slot 2 — Type (0x1800066d0, `MOV EAX,dword[0x1801f9b38]`).
    using TypeFn = uint32_t (*)(const void*);
    CHECK_EQ(reinterpret_cast<TypeFn>(vt[RetailSlot<TestExternal>(2)])(self), Object::External::TypeId());

    // slot 3 — GetFileVersion. Pure in External; the leaf overrides it.
    using VersionFn = int32_t (*)(const void*);
    CHECK_EQ(reinterpret_cast<VersionFn>(vt[RetailSlot<TestExternal>(3)])(self), 7);

    // slot 4 — External::Compile (0x1800066e0, `MOV EAX,0x1; RET`). The leaf
    // does NOT override it, so this lands on External's own body. `path` is
    // genuinely dead in retail: RDX is never read in the two instructions.
    using CompileFn = uint32_t (*)(void*, const System::Filename*);
    System::Filename dead;
    CHECK_EQ(reinterpret_cast<CompileFn>(vt[RetailSlot<TestExternal>(4)])(self, &dead), 1u);

    // slot 5 — Serialize.
    g_serializeCount = 0;
    using SerFn = bool (*)(void*, void*);
    CHECK(reinterpret_cast<SerFn>(vt[RetailSlot<TestExternal>(5)])(self, nullptr));
    CHECK_EQ(g_serializeCount, 1);

    // slots 6 and 7 — Reconstruct and Destruct. Their names come from
    // POSITION off `??_7ExternalList@Object@@6B@` (External's own two are
    // `_purecall`), so this is the check that the declaration order in
    // External.h matches that reading.
    using ReconFn = Object::External* (*)(void*);
    CHECK(reinterpret_cast<ReconFn>(vt[RetailSlot<TestExternal>(6)])(self) == static_cast<Object::External*>(&obj));
    g_destructCount = 0;
    using DestructFn = void (*)(void*);
    reinterpret_cast<DestructFn>(vt[RetailSlot<TestExternal>(7)])(self);
    CHECK_EQ(g_destructCount, 1);

    // ...and the same answers through ordinary C++ dispatch (the other
    // direction of the slot-for-slot interchangeability).
    Object::Base* asBase = &obj;
    // The class record (DEFINE_TYPE, ENDGAME B.3.9 OBJ): no Factory::Startup in
    // the unit harness, so the id is the ctor's sentinel; what matters is that
    // slot 2, TypeId() and IsA agree on the ONE record.
    CHECK_EQ(asBase->Type(), Object::External::TypeId());
    CHECK_STREQ(asBase->TypeName(), "External");
    CHECK_STREQ(Object::External::GetTypeInfo().parentName, "Base");
    CHECK(Object::External::GetTypeInfo().fileExtension == nullptr);

    // `IsA` is an EXACT-type test, not a derivation test (0x1800491c0).
    CHECK(asBase->IsA(Object::External::TypeId()));
    CHECK(!asBase->IsA(Object::External::TypeId() ^ 1u));

    // The leaf's own field survived construction through the whole chain.
    CHECK_EQ(obj._marker, 0xABCDEF01u);
}

// ---------------------------------------------------------------------------
// 3/4. Slot 0 and Object::Destroy
// ---------------------------------------------------------------------------
UNIT_TEST(object_external_destroy_reaches_slot0_with_flags1) {
    // `Destroy(p)` is `delete p` (B3X W1), and the deleting destructor FREES
    // the storage — so the object has to come from the allocator its operator
    // delete frees with: `Object::TypeTreeFree` (the seam type_reflection.cpp
    // exposes, a malloc branch under -DBANISHED_UNIT_TEST).
    void* raw = Object::TypeTreeAlloc(sizeof(TestExternal), alignof(TestExternal));
    CHECK(raw != nullptr);
    if (raw == nullptr) return;

    TestExternal* p = new (raw) TestExternal();

    // Retail flags 0 — destroy WITHOUT freeing, which is what `Reconstruct`
    // and `Destruct` drive — is D1, compiler slot 0, read raw here; D0 is the
    // next slot and a different body.
    void** vt = VTableOf(p);
    CHECK(vt[0] != vt[1]);
    g_dtorCount = 0; dtor_check::ResetFrees();
    reinterpret_cast<dtor_check::DtorEntry>(vt[0])(p);
    CHECK_EQ(g_dtorCount, 1);
    CHECK_EQ(dtor_check::g_frees, 0);

    // Rebuild in the same storage and let Destroy take the free arm (D0).
    p = new (raw) TestExternal();
    g_dtorCount = 0; dtor_check::ResetFrees();
    Object::Destroy(p);
    CHECK_EQ(g_dtorCount, 1);
    CHECK_EQ(dtor_check::g_frees, 1);
    CHECK(dtor_check::g_lastFree == raw);

    // The null test is retail's `Destroy` shape (`if (p) <slot 0>(p, 1)`), not
    // a guard on a faithful deref: `delete` of a null pointer to a class with a
    // virtual destructor runs nothing and frees nothing.
    Object::Destroy(nullptr);
    CHECK_EQ(g_dtorCount, 1);
    CHECK_EQ(dtor_check::g_frees, 1);
}

// ---------------------------------------------------------------------------
// 5a. The 0x18-element resource tree — root and two levels down
//
//            "mmm"                  depth 0 (root)
//           /     \
//       "ddd"     "ttt"             depth 1
//       /   \     /   \
//   "bbb" "ggg" "ppp" "zzz"         depth 2
// ---------------------------------------------------------------------------
UNIT_TEST(object_external_resource_find_at_two_depths) {
    using Node = Object::ResourceSet::Node;
    // Raw node storage, as the factory's own nodes are (the element has a
    // constructor and a ~Filename, so a `Node n[7]` would build and release them).
    alignas(Node) unsigned char storage[7 * sizeof(Node)];
    std::memset(storage, 0, sizeof storage);
    Node* n = reinterpret_cast<Node*>(storage);

    static char kMmm[] = "mmm"; static char kDdd[] = "ddd";
    static char kTtt[] = "ttt"; static char kBbb[] = "bbb";
    static char kGgg[] = "ggg"; static char kPpp[] = "ppp";
    static char kZzz[] = "zzz";
    char* keys[7] = { kMmm, kDdd, kTtt, kBbb, kGgg, kPpp, kZzz };
    for (int i = 0; i < 7; ++i) n[i].element.path._data = keys[i];

    n[0].left = &n[1]; n[0].right = &n[2];
    n[1].parent = &n[0]; n[2].parent = &n[0];
    n[1].left = &n[3]; n[1].right = &n[4];
    n[2].left = &n[5]; n[2].right = &n[6];
    n[3].parent = &n[1]; n[4].parent = &n[1];
    n[5].parent = &n[2]; n[6].parent = &n[2];

    // Driven over the tree ROOT, not over a `Collection::Set` object: a `Set`
    // has a real `~Set` that reaches set_core.cpp's allocator doors, which do
    // not link here. Both spellings are the same body (External.h).
    Node* root = &n[0];

    CHECK(Object::detail::ResourceFind(root, "mmm") == &n[0]);   // root
    CHECK(Object::detail::ResourceFind(root, "ddd") == &n[1]);   // depth 1
    CHECK(Object::detail::ResourceFind(root, "ttt") == &n[2]);
    // THE FOUR THAT MATTER: a find on the wrong node layout answers the root
    // correctly and every one of these incorrectly.
    CHECK(Object::detail::ResourceFind(root, "bbb") == &n[3]);   // depth 2
    CHECK(Object::detail::ResourceFind(root, "ggg") == &n[4]);
    CHECK(Object::detail::ResourceFind(root, "ppp") == &n[5]);
    CHECK(Object::detail::ResourceFind(root, "zzz") == &n[6]);
    CHECK(Object::detail::ResourceFind(root, "qqq") == nullptr);

    // The resource tree is case-INSENSITIVE (retail's `_stricmp` in the "C"
    // locale). `ExternalList::Find` over the same kind of path is
    // case-SENSITIVE; both are transcribed as found and must not be harmonised.
    CHECK(Object::detail::ResourceFind(root, "GGG") == &n[4]);
    CHECK(Object::detail::ResourceFind(root, "MMM") == &n[0]);

    // A null key orders below everything (System::Filename::operator<).
    CHECK(Object::detail::ResourceFind(root, nullptr) == nullptr);

    // The in-order successor walk FindNext drives. It reads `parent`, i.e. the
    // field this element width puts at +0x18 — the other half of the layout
    // claim, and the half a find alone would not exercise.
    CHECK(Object::detail::ResourceLeftmost(root) == &n[3]);
    CHECK(Object::detail::ResourceSuccessor(&n[3]) == &n[1]);
    CHECK(Object::detail::ResourceSuccessor(&n[1]) == &n[4]);
    CHECK(Object::detail::ResourceSuccessor(&n[4]) == &n[0]);   // climbs two parents
    CHECK(Object::detail::ResourceSuccessor(&n[0]) == &n[5]);
    CHECK(Object::detail::ResourceSuccessor(&n[6]) == nullptr); // the last
}

// ---------------------------------------------------------------------------
// 5b. The 0x10-element reference tree — same shape, OTHER node layout
// ---------------------------------------------------------------------------
UNIT_TEST(object_external_reference_find_at_two_depths) {
    using Node = Object::ReferenceSet::Node;
    // Raw node storage, as the factory's own nodes are (the element has a
    // constructor and a ~Filename, so a `Node n[7]` would build and release them).
    alignas(Node) unsigned char storage[7 * sizeof(Node)];
    std::memset(storage, 0, sizeof storage);
    Node* n = reinterpret_cast<Node*>(storage);

    const uint32_t ids[7] = { 50, 30, 70, 20, 40, 60, 80 };
    for (int i = 0; i < 7; ++i) n[i].element.id = ids[i];

    n[0].left = &n[1]; n[0].right = &n[2];
    n[1].parent = &n[0]; n[2].parent = &n[0];
    n[1].left = &n[3]; n[1].right = &n[4];
    n[2].left = &n[5]; n[2].right = &n[6];
    n[3].parent = &n[1]; n[4].parent = &n[1];
    n[5].parent = &n[2]; n[6].parent = &n[2];

    Node* root = &n[0];   // see the resource test: no `Set` object here either

    CHECK(Object::detail::ReferenceFind(root, 50) == &n[0]);   // root
    CHECK(Object::detail::ReferenceFind(root, 30) == &n[1]);   // depth 1
    CHECK(Object::detail::ReferenceFind(root, 70) == &n[2]);
    CHECK(Object::detail::ReferenceFind(root, 20) == &n[3]);   // depth 2
    CHECK(Object::detail::ReferenceFind(root, 40) == &n[4]);
    CHECK(Object::detail::ReferenceFind(root, 60) == &n[5]);
    CHECK(Object::detail::ReferenceFind(root, 80) == &n[6]);
    CHECK(Object::detail::ReferenceFind(root, 55) == nullptr);
    CHECK(Object::detail::ReferenceFind(root, 0)  == nullptr);

    // The two layouts really ARE different — which is the whole reason the trap
    // exists and the reason both finds above had to be driven below the root.
    CHECK(sizeof(Object::ResourceSet::Node) == 0x38);
    CHECK(sizeof(Object::ReferenceSet::Node) == 0x30);
    CHECK(offsetof(Object::ResourceSet::Node, parent) == 0x18);
    CHECK(offsetof(Object::ResourceSet::Node, left)   == 0x20);
    CHECK(offsetof(Object::ResourceSet::Node, right)  == 0x28);
    CHECK(offsetof(Object::ReferenceSet::Node, parent) == 0x10);
    CHECK(offsetof(Object::ReferenceSet::Node, left)   == 0x18);
    CHECK(offsetof(Object::ReferenceSet::Node, right)  == 0x20);
}

// ---------------------------------------------------------------------------
// The virtual-destructor rule, restated for this family (B3X W1)
//
// Retail slot 0 is the deleting destructor; the family now declares
// `virtual ~T()`, which costs TWO slots (D1 complete + D0 deleting), so retail
// slot N >= 1 is compiler slot N + 1 — the translation the per-slot checks
// above make through RetailSlot<TestExternal>.
// ---------------------------------------------------------------------------
UNIT_TEST(object_external_family_has_a_virtual_destructor) {
    CHECK(std::has_virtual_destructor<Object::External>::value);
    CHECK(std::has_virtual_destructor<Object::ExternalList>::value);
    CHECK(std::has_virtual_destructor<Object::CompiledExternal>::value);
    CHECK(std::has_virtual_destructor<Object::Base>::value);
    CHECK_EQ(RetailSlot<Object::External>(1), 2L);
}
