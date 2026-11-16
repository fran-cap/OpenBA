// ============================================================================
// src/engine/audio/audio_type_records.cpp — the Audio type records (ENDGAME
// B.3.9 TI-REST, B39_RULES D6: ONE `DEFINE_TYPE`).
//
// `?_typeInfo@SoundBuffer@Audio@@` (retail 0x1801fa4d8) and
// `?_typeInfo@SoundEffect@Audio@@` (0x1801fa510) were MINTED until this block;
// the classes' own records, built at CRT init and head-inserted into
// `Object::BaseTypeInfo::_firstTypeInfo`, are the only ones now.
//
// THE LITERALS ARE THE PERSISTED ABI (ids are `~CRC32(name)`), copied from the
// minted rows and retail's literal bodies, never respelt:
//   SoundBuffer  parent "Object::CompiledExternal" (?_ParentTypeName@ 0x18000b880)
//                ext nullptr, factory pair 0x18000b9a0 / 0x18000ba90
//   SoundEffect  parent "Object::External"         (?_ParentTypeName@ 0x180050310)
//                ext nullptr, factory pair 0x180050430 / 0x1800504f0
//
// Its own translation unit so the unit harness links it without the classes'
// registry-bound bodies (tests/unit/b39_ti_rest_test.cpp).
// ============================================================================
#include "audio/SoundBuffer.h"
#include "audio/SoundEffect.h"

DEFINE_TYPE(Audio::SoundBuffer, "Object::CompiledExternal", nullptr)
DEFINE_TYPE(Audio::SoundEffect, "Object::External", nullptr)
