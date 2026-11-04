// ============================================================================
// tests/unit/review_object_external_test.cpp -- review findings F-122, F-123,
// F-143, F-144, F-145 (docs/history/review_20261103/findings_p8.json)
//
// A type id read from a resource or a save that names no registered type. Retail's
// `Factory::CreateObject` (0x1800c0ae0) falls out of the descent with RAX = 0 and
// loads [RAX+0x10]: a null dereference, which is undefined behaviour in C++. The
// port returns null instead. External::Load (0x1800bc620) and
// InternalFactory::PersistObject (0x1800bff10) open-code the same descent; neither
// links into the unit build (External::Load stands on the file streams, and
// PersistObject sits behind BANISHED_UNIT_TEST in internal_class.cpp), so the miss
// arm is driven here through the one body that does link.
// ============================================================================
#include "unit.h"

#include <cstdint>

#include "object/TypeInfo.h"
#include "object/Internal.h"

namespace {

struct ChainScope {
    ::Object::BaseTypeInfo* saved;
    ChainScope() : saved(::Object::BaseTypeInfo::_firstTypeInfo) {
        ::Object::BaseTypeInfo::_firstTypeInfo = nullptr;
    }
    ~ChainScope() { ::Object::BaseTypeInfo::_firstTypeInfo = saved; }
};

int g_created = 0;

::Object::Base* CountingFactory() {
    ++g_created;
    return nullptr;   // an abstract type's factory answers null
}
::Object::Base* CountingInPlace(void* /*storage*/) {
    ++g_created;
    return nullptr;
}

}  // namespace

// F-144: an unregistered type id. Before the fix: a read at address 0x10.
UNIT_TEST(review_object_external_create_object_miss_returns_null) {
    ChainScope scope;
    ::Object::Factory factory;
    ::Object::BaseTypeInfo a("ReviewTypeA", nullptr, nullptr, &CountingFactory, &CountingInPlace);
    ::Object::BaseTypeInfo b("ReviewTypeB", "ReviewTypeA", nullptr, &CountingFactory, &CountingInPlace);
    factory.Startup();

    g_created = 0;
    const uint32_t missing = ::Object::TypeNameCRC("ReviewTypeNotRegistered");
    CHECK(factory.GetTypes().Find(missing) == nullptr);
    CHECK(factory.CreateObject(missing) == nullptr);
    unsigned char storage[16] = {};
    CHECK(factory.CreateObject(missing, storage) == nullptr);
    CHECK_EQ(g_created, 0);   // no factory function ran on a miss

    // A registered id still reaches its factory function, exactly once per call.
    CHECK(factory.CreateObject(::Object::TypeNameCRC("ReviewTypeB")) == nullptr);
    CHECK_EQ(g_created, 1);
    CHECK(factory.CreateObject(::Object::TypeNameCRC("ReviewTypeA"), storage) == nullptr);
    CHECK_EQ(g_created, 2);
}

// The same miss on an empty tree (no type registered at all).
UNIT_TEST(review_object_external_create_object_on_empty_factory_returns_null) {
    ::Object::Factory factory;
    CHECK(factory.CreateObject(0x1234u) == nullptr);
    CHECK(factory.CreateObject(0x1234u, nullptr) == nullptr);
}

// F-143: the type test of an InternalPtr<T> field runs on the held Internal*, so a
// null field answers false without any downcast.
UNIT_TEST(review_object_external_internal_ptr_type_test_on_null) {
    ::Object::InternalBasePtr empty;
    CHECK(::Object::IsDerivedFromNullSafe(empty, 0x1234u) == false);
}

// ---------------------------------------------------------------------------
// F-143 with a wrong-type object. A save names a live Internal of another type in
// an `InternalPtr<T>` field; the load path null-tests the field, then type-tests it
// and nulls it on a mismatch. Both tests run on the held Internal*: `IsNull()` and
// `IsDerivedFromNullSafe(const InternalBasePtr&, id)` never instantiate
// `InternalPtr<T>::Get()` (the static_cast to T*), which on this object would be
// undefined behaviour. Before the fix the null test was spelled `field.Get() !=
// nullptr` at 26 load sites and `IsNull()` did not exist.
// ---------------------------------------------------------------------------
namespace Object {
namespace detail {
extern InternalFactory* g_unitInternalFactory;
}  // namespace detail
}  // namespace Object

namespace {

struct InternalFactoryScope {
    ::Object::InternalFactory  factory;
    ::Object::InternalFactory* saved;
    InternalFactoryScope() : factory(), saved(::Object::detail::g_unitInternalFactory) {
        ::Object::detail::g_unitInternalFactory = &factory;
    }
    ~InternalFactoryScope() { ::Object::detail::g_unitInternalFactory = saved; }
};

struct TypeFactoryScope {
    ::Object::Factory* saved;
    explicit TypeFactoryScope(::Object::Factory* f) : saved(::Object::Factory::_singleton) {
        ::Object::Factory::_singleton = f;
    }
    ~TypeFactoryScope() { ::Object::Factory::_singleton = saved; }
};

// Two unrelated Internal types under one root. `Type()` answers the CRC the local
// records register, the way DECLARE_TYPE's override answers its own record.
class ReviewWrongT : public ::Object::Internal {
public:
    const char* TypeName() const override { return "ReviewWrongT"; }
    uint32_t Type() const override { return ::Object::TypeNameCRC("ReviewWrongT"); }
};
class ReviewWrongU : public ::Object::Internal {
public:
    const char* TypeName() const override { return "ReviewWrongU"; }
    uint32_t Type() const override { return ::Object::TypeNameCRC("ReviewWrongU"); }
};

// Never defined: an `InternalPtr` to it cannot instantiate `Get()`, so a path that
// compiles on it has no downcast.
class ReviewNeverDefined;

// The load sites' shape (completetask.cpp, pickup.cpp, ...): null test, type test,
// null the field on a mismatch.
template <class T>
void ValidateLoadedField(::Object::InternalPtr<T>& field, uint32_t typeId) {
    if (!field.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(field, typeId)) {
            field = ::Object::InternalPtr<T>();
        }
    }
}

}  // namespace

UNIT_TEST(review_object_external_internal_ptr_type_test_on_wrong_type) {
    ChainScope chain;
    ::Object::Factory types;
    ::Object::BaseTypeInfo root("ReviewF143Root", nullptr, nullptr, &CountingFactory, &CountingInPlace);
    ::Object::BaseTypeInfo t("ReviewWrongT", "ReviewF143Root", nullptr, &CountingFactory, &CountingInPlace);
    ::Object::BaseTypeInfo u("ReviewWrongU", "ReviewF143Root", nullptr, &CountingFactory, &CountingInPlace);
    types.Startup();
    TypeFactoryScope typeScope(&types);
    InternalFactoryScope internals;

    const uint32_t tidRoot = ::Object::TypeNameCRC("ReviewF143Root");
    const uint32_t tidT    = ::Object::TypeNameCRC("ReviewWrongT");
    const uint32_t tidU    = ::Object::TypeNameCRC("ReviewWrongU");

    ReviewWrongT rightObject;
    ReviewWrongU wrongObject;

    // A field of type T holding a U: what a save that names the wrong type loads.
    ::Object::InternalPtr<ReviewWrongT> field;
    static_cast<::Object::InternalBasePtr&>(field) = ::Object::InternalBasePtr(&wrongObject);
    CHECK(!field.IsNull());
    CHECK(::Object::IsDerivedFromNullSafe(field, tidT) == false);
    CHECK(::Object::IsDerivedFromNullSafe(field, tidU) == true);
    CHECK(::Object::IsDerivedFromNullSafe(field, tidRoot) == true);
    ValidateLoadedField(field, tidT);
    CHECK(field.IsNull());

    // The right type survives the same load-site test.
    ::Object::InternalPtr<ReviewWrongT> good(&rightObject);
    ValidateLoadedField(good, tidT);
    CHECK(!good.IsNull());
    CHECK(good.Get() == &rightObject);

    // A null field stays null and never reaches the type test.
    ::Object::InternalPtr<ReviewWrongT> empty;
    ValidateLoadedField(empty, tidT);
    CHECK(empty.IsNull());

    // The same path on a field whose T is incomplete: it compiles, so neither the
    // null test nor the type test downcasts.
    ::Object::InternalPtr<ReviewNeverDefined> opaque;
    static_cast<::Object::InternalBasePtr&>(opaque) = ::Object::InternalBasePtr(&wrongObject);
    CHECK(!opaque.IsNull());
    ValidateLoadedField(opaque, tidT);
    CHECK(opaque.IsNull());
}
