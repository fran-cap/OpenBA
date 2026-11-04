// ============================================================================
// tests/unit/review_object_reference_id_test.cpp -- review finding F-172
// (docs/history/review_20261103/findings_p8.json)
//
// The reference table (`ExternalFactory::_references`) is ordered on the id as a
// SIGNED 32-bit key: retail's insert (0x18002d1d7 `CMP ECX,EAX; JL; CMP EAX,ECX;
// JGE`, ported in reference_set.cpp) and retail's lookup
// `GetPathForReferenceId` (0x1800bf1d0 `CMP R8D,EAX; JL; CMP EAX,R8D; JGE`)
// agree. The port's descent compared unsigned, so an id with the top bit set,
// placed LEFT of the small ids by the insert, was searched for on the right.
// The tree below is laid out the way the signed insert lays out ids 1, 2 and
// 0x80000001: 0x80000001 (= INT32_MIN + 1) is the smallest key.
// ============================================================================
#include "unit.h"

#include <cstdint>
#include <cstring>

#include "object/External.h"

UNIT_TEST(review_object_reference_find_orders_ids_signed) {
    using Node = Object::ReferenceSet::Node;
    alignas(Node) unsigned char storage[5 * sizeof(Node)];
    std::memset(storage, 0, sizeof storage);
    Node* n = reinterpret_cast<Node*>(storage);

    //                 1
    //         0x80000001    2
    //   0xFFFFFFFF(-1)  0  ...
    // Signed order: 0x80000001 < 0xFFFFFFFF (-1) < 0 < 1 < 2.
    n[0].element.id = 1u;
    n[1].element.id = 0x80000001u;
    n[2].element.id = 2u;
    n[3].element.id = 0u;            // right child of 0x80000001
    n[4].element.id = 0xFFFFFFFFu;   // left child of 0
    n[0].left = &n[1];
    n[0].right = &n[2];
    n[1].parent = &n[0];
    n[2].parent = &n[0];
    n[1].right = &n[3];
    n[3].parent = &n[1];
    n[3].left = &n[4];
    n[4].parent = &n[3];

    Node* root = &n[0];
    CHECK(Object::detail::ReferenceFind(root, 1u) == &n[0]);
    CHECK(Object::detail::ReferenceFind(root, 2u) == &n[2]);
    CHECK(Object::detail::ReferenceFind(root, 0x80000001u) == &n[1]);
    CHECK(Object::detail::ReferenceFind(root, 0u) == &n[3]);
    CHECK(Object::detail::ReferenceFind(root, 0xFFFFFFFFu) == &n[4]);
    CHECK(Object::detail::ReferenceFind(root, 3u) == nullptr);
    CHECK(Object::detail::ReferenceFind(root, 0x80000000u) == nullptr);
}
