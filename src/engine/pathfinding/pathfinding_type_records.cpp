// ============================================================================
// src/engine/pathfinding/pathfinding_type_records.cpp — the Pathfinding type
// record (ENDGAME B.3.9 TI-REST, B39_RULES D6: ONE `DEFINE_TYPE`).
//
// `?_typeInfo@PointList@Pathfinding@@0VBaseTypeInfo@Object@@A` (retail
// 0x1801fa858) was MINTED until this block; the class's own record, built at
// CRT init and head-inserted into `Object::BaseTypeInfo::_firstTypeInfo`, is
// the only one now.
//
// THE LITERALS ARE THE PERSISTED ABI (ids are `~CRC32(name)`), copied from the
// minted row and retail's literal bodies, never respelt:
//   name   "PointList"
//   parent "Object::External"  (?_ParentTypeName@PointList@Pathfinding@@)
//   ext    nullptr
//   factory pair: the class's own statics 0x180070f30 / 0x180070ff0.
//
// Its own translation unit so the unit harness links it without pointlist.cpp
// (tests/unit/b39_ti_rest_test.cpp).
// ============================================================================
#include "pathfinding/PointList.h"

DEFINE_TYPE(Pathfinding::PointList, "Object::External", nullptr)
