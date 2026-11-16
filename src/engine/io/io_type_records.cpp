// ============================================================================
// src/engine/io/io_type_records.cpp — the IO type record (ENDGAME B.3.9
// TI-REST, B39_RULES D6: ONE `DEFINE_TYPE`).
//
// `?_typeInfo@PackageFile@IO@@0VBaseTypeInfo@Object@@A` (retail 0x1801f9ac8)
// was MINTED until this block; the class's own record, built at CRT init and
// head-inserted into `Object::BaseTypeInfo::_firstTypeInfo`, is the only one
// now.
//
// THE LITERALS ARE THE PERSISTED ABI (ids are `~CRC32(name)`), copied from the
// minted row and retail's literal bodies, never respelt:
//   name   "PackageFile"
//   parent "Object::External"  (?_ParentTypeName@PackageFile@IO@@)
//   ext    nullptr             (the record's +0x18 in retail's .data)
//   factory pair: the class's own statics 0x180025240 / 0x1800252c0.
//
// Its own translation unit so the unit harness links it without
// package_file.cpp (tests/unit/b39_ti_rest_test.cpp).
// ============================================================================
#include "io/PackageFile.h"

DEFINE_TYPE(IO::PackageFile, "Object::External", nullptr)
