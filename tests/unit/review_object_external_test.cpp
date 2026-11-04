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
