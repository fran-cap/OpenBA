// ============================================================================
// src/engine/collision2d/collision2d_type_records.cpp — the Collision2D type
// records (ENDGAME B.3.9 TI-REST, B39_RULES D6: ONE `DEFINE_TYPE`).
//
// `?_typeInfo@CollisionMesh@Collision2D@@` (retail 0x1801fa7e8) and
// `?_typeInfo@Surface@Collision2D@@` (0x1801fa820) were MINTED until this
// block; the classes' own records, built at CRT init and head-inserted into
// `Object::BaseTypeInfo::_firstTypeInfo`, are the only ones now.
//
// THE LITERALS ARE THE PERSISTED ABI (ids are `~CRC32(name)`), copied from the
// minted rows and retail's literal bodies, never respelt:
//   CollisionMesh  parent "Object::External", ext nullptr,
//                  factory pair 0x180065060 / 0x180065130
//   Surface        parent "Object::External", ext nullptr,
//                  factory pair 0x180064b60 / 0x180064c10
//
// Its own translation unit so the unit harness links it without the classes'
// bodies (tests/unit/b39_ti_rest_test.cpp).
// ============================================================================
#include "collision2d/CollisionMesh.h"
#include "collision2d/Surface.h"

DEFINE_TYPE(Collision2D::CollisionMesh, "Object::External", nullptr)
DEFINE_TYPE(Collision2D::Surface, "Object::External", nullptr)
