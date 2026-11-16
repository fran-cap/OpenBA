// ============================================================================
// src/engine/graphics/graphics_type_records.cpp — the Graphics type records
// (ENDGAME B.3.9 TI-REST, B39_RULES D6: ONE `DEFINE_TYPE`).
//
// The eleven `?_typeInfo@…@Graphics@@` records were MINTED until this block;
// the classes' own records, built at CRT init and head-inserted into
// `Object::BaseTypeInfo::_firstTypeInfo`, are the only ones now.
//
// THE LITERALS ARE THE PERSISTED ABI (ids are `~CRC32(name)`; saves and .rsc
// files carry them), copied from the minted rows and retail's
// `_ParentTypeName` bodies (B35_SLOTS section 3), NEVER derived from a C++
// base name -- GeneratedImageBuffer's parent is namespaced, its two subclasses'
// is not, ParticleSystem's is the bare "Particle":
//
//   class                  retail record  parent literal
//   AnimationEvent         0x1801f9978    "Object::External"
//   AnimationGroup         0x1801fa6d0    "Object::External"
//   EdgeMesh               0x1801f99b0    "Object::External"
//   GraphicsMesh           0x1801fa708    "Object::CompiledExternal"
//   Material               0x1801fa740    "Object::External"
//   MaterialInstance       0x1801fa778    "Object::External"
//   Particle               0x1801fa7b0    "Object::External"
//   ParticleSystem         0x1801f9a58    "Particle"
//   GeneratedImageBuffer   0x1801f9a20    "Video::ImageBuffer"
//   FrameCopyImageBuffer   0x1801f99e8    "GeneratedImageBuffer"
//   ReflectionImageBuffer  0x1801f9a90    "GeneratedImageBuffer"
//
// ext nullptr for all eleven (the records' +0x18 in retail's .data). Factory
// pairs: each class's own statics (AnimationEvent's and Particle's the
// ABSTRACT classes' `XOR EAX,EAX` pairs), defined in the classes' TUs and
// handed to the record by DEFINE_TYPE.
//
// Its own translation unit so the unit harness links it without the classes'
// bodies (tests/unit/b39_ti_rest_test.cpp).
// ============================================================================
#include "graphics/Animation.h"
#include "graphics/AnimationGroup.h"
#include "graphics/Mesh.h"
#include "graphics/GraphicsMesh.h"
#include "graphics/Material.h"
#include "graphics/Particle.h"
#include "graphics/GeneratedImageBuffer.h"
#include "graphics/FrameCopyImageBuffer.h"

DEFINE_TYPE(Graphics::AnimationEvent,        "Object::External",         nullptr)
DEFINE_TYPE(Graphics::AnimationGroup,        "Object::External",         nullptr)
DEFINE_TYPE(Graphics::EdgeMesh,              "Object::External",         nullptr)
DEFINE_TYPE(Graphics::GraphicsMesh,          "Object::CompiledExternal", nullptr)
DEFINE_TYPE(Graphics::Material,              "Object::External",         nullptr)
DEFINE_TYPE(Graphics::MaterialInstance,      "Object::External",         nullptr)
DEFINE_TYPE(Graphics::Particle,              "Object::External",         nullptr)
DEFINE_TYPE(Graphics::ParticleSystem,        "Particle",                 nullptr)
DEFINE_TYPE(Graphics::GeneratedImageBuffer,  "Video::ImageBuffer",       nullptr)
DEFINE_TYPE(Graphics::FrameCopyImageBuffer,  "GeneratedImageBuffer",     nullptr)
DEFINE_TYPE(Graphics::ReflectionImageBuffer, "GeneratedImageBuffer",     nullptr)
