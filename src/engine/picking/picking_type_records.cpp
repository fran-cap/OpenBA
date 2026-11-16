// ============================================================================
// src/engine/picking/picking_type_records.cpp — the Picking type record
// (ENDGAME B.3.9 TI-REST, B39_RULES D6: ONE `DEFINE_TYPE`).
//
// `?_typeInfo@PickingMesh@Picking@@0VBaseTypeInfo@Object@@A` (retail
// 0x1801faf20) was MINTED until this block; the class's own record, built at
// CRT init and head-inserted into `Object::BaseTypeInfo::_firstTypeInfo`, is
// the only one now.
//
// THE LITERALS ARE THE PERSISTED ABI (ids are `~CRC32(name)`; saves and .rsc
// files carry them), copied from the minted row and retail's literal bodies,
// never respelt:
//   name   "PickingMesh"       (the stringised class, namespace stripped)
//   parent "Object::External"  (?_ParentTypeName@PickingMesh@Picking@@ 0x180071a80)
//   ext    nullptr             (the record's +0x18 in retail's .data)
//   factory pair: the class's own statics 0x180071ba0 / 0x180071c60.
//
// The records live in their own translation unit (not pickingmesh.cpp) so the
// unit harness can link them without the class's other bodies:
// tests/unit/b39_ti_rest_test.cpp runs a Factory::Startup over them.
// ============================================================================
#include "picking/PickingMesh.h"

DEFINE_TYPE(Picking::PickingMesh, "Object::External", nullptr)
