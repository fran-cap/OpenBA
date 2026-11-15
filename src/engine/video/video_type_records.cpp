// ============================================================================
// src/engine/video/video_type_records.cpp — the Video type records (ENDGAME
// B.3.9 TI-REST, B39_RULES D6: ONE `DEFINE_TYPE`).
//
// The seven `?_typeInfo@…@Video@@` records were MINTED until this block; the
// classes' own records, built at CRT init and head-inserted into
// `Object::BaseTypeInfo::_firstTypeInfo`, are the only ones now.
//
// THE LITERALS ARE THE PERSISTED ABI (ids are `~CRC32(name)`; .rsc files
// carry them), copied from the minted rows and retail's `_TypeName` /
// `_ParentTypeName` bodies, never respelt ("qualified iff the parent is in a
// different namespace" is a pattern, not a rule -- every one is copied):
//
//   class           retail record  parent literal               ext
//   ImageBuffer     0x1801fa548    "Object::CompiledExternal"   nullptr
//   ShaderProgram   0x1801fa628    "Object::CompiledExternal"   nullptr
//   VertexProgram   0x1801fa698    "ShaderProgram"              nullptr
//   PixelProgram    0x1801fa580    "ShaderProgram"              nullptr
//   VertexLayout    0x1801fa660    "Object::CompiledExternal"   nullptr
//   RenderState     0x1801fa5f0    "Object::External"           nullptr
//   VideoProgram    0x1801fa5b8    "Object::External"           nullptr
//
// ext is the records' +0x18 in retail's .data: nullptr for all seven, even for
// the two programs whose `_FileExtension` returns "srsl" (a separate static).
// Factory pairs: each class's own statics (ShaderProgram's the ABSTRACT
// class's `XOR EAX,EAX` pair), defined in the class's TU.
//
// Its own translation unit so the unit harness links it without the classes'
// bodies (tests/unit/b39_ti_rest_test.cpp).
// ============================================================================
#include "video/ImageBuffer.h"
#include "video/ShaderProgram.h"
#include "video/VertexLayout.h"
#include "video/RenderState.h"
#include "video/VideoProgram.h"

DEFINE_TYPE(Video::ImageBuffer,   "Object::CompiledExternal", nullptr)
DEFINE_TYPE(Video::ShaderProgram, "Object::CompiledExternal", nullptr)
DEFINE_TYPE(Video::VertexProgram, "ShaderProgram",            nullptr)
DEFINE_TYPE(Video::PixelProgram,  "ShaderProgram",            nullptr)
DEFINE_TYPE(Video::VertexLayout,  "Object::CompiledExternal", nullptr)
DEFINE_TYPE(Video::RenderState,   "Object::External",         nullptr)
DEFINE_TYPE(Video::VideoProgram,  "Object::External",         nullptr)
