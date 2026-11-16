// ============================================================================
// src/engine/graphics/graphics_typeinfo_accessors.cpp — WAVE 21 lane GFXVT
// ============================================================================
//
// EIGHT VTABLE SLOTS ACROSS THREE Graphics `??_7` ROWS: TWO BODIES HERE, SIX DECLARE_TYPE's.
//
// THE BODIES, READ FROM THE SHIPPED Runtime-x64.dll BYTES (not from Ghidra's C,
// which drops dereferences). Each
// RIP displacement was resolved by hand against the NEXT instruction address:
//
//   ?Type@AnimationEvent@Graphics@@UEBAIXZ        0x1800116d0
//       8b 05 a2 82 1e 00 c3     MOV EAX,[0x1801f9978]   = node+0x00
//   ?TypeName@AnimationEvent@Graphics@@UEBAPEBDXZ 0x1800116f0
//       48 8b 05 89 82 1e 00 c3  MOV RAX,[0x1801f9980]   = node+0x08
//
//   ?Type@MaterialInstance@Graphics@@UEBAIXZ        0x1800027f0
//       8b 05 82 7f 1f 00 c3     MOV EAX,[0x1801fa778]   = node+0x00
//   ?TypeName@MaterialInstance@Graphics@@UEBAPEBDXZ 0x180002810
//       48 8b 05 69 7f 1f 00 c3  MOV RAX,[0x1801fa780]   = node+0x08
//
//   ?Type@Particle@Graphics@@UEBAIXZ        0x18001ff10
//       8b 05 9a a8 1d 00 c3     MOV EAX,[0x1801fa7b0]   = node+0x00
//   ?TypeName@Particle@Graphics@@UEBAPEBDXZ 0x18001ff30
//       48 8b 05 81 a8 1d 00 c3  MOV RAX,[0x1801fa7b8]   = node+0x08
//
//   ?GetFileVersion@MaterialInstance@Graphics@@UEBAHXZ 0x180103a60
//       b8 04 00 00 00 c3        MOV EAX,4    ; RET      -> literal 4
//   ?GetFileVersion@Particle@Graphics@@UEBAHXZ         0x180104080
//       b8 00 10 00 00 c3        MOV EAX,0x1000 ; RET    -> literal 4096
//
// `this` is read by NONE of the eight. That is not an inference from the
// decompile: the encodings above have no RCX operand at all.
//
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "graphics/Animation.h"   // Graphics::AnimationEvent -- block GC
#include "graphics/Material.h"    // Graphics::MaterialInstance -- ditto
#include "graphics/Particle.h"    // Graphics::Particle -- ditto


namespace {

// ---- the BaseTypeInfo fields slots 1 and 2 read ------------------------------
//
// BaseTypeInfo's first two fields, the ONLY two DECLARE_TYPE's Type / TypeName read:
//     +0x00  uint32_t     typeId
//     +0x08  const char*  name
constexpr size_t kTypeInfoTypeIdOffset = 0x00;
constexpr size_t kTypeInfoNameOffset   = 0x08;
static_assert(kTypeInfoTypeIdOffset == 0, "BaseTypeInfo::typeId is at +0x00");
static_assert(kTypeInfoNameOffset == 8,   "BaseTypeInfo::name is at +0x08");
static_assert(sizeof(uint32_t) == 4,      "typeId is a DWORD (MOV EAX, not RAX)");
static_assert(sizeof(const char*) == 8,   "name is a QWORD (MOV RAX)");


typedef uint32_t    (*U32ThisFn)(const void*);
typedef const char* (*CStrThisFn)(const void*);
typedef int32_t     (*I32ThisFn)(const void*);


}  // namespace

// ############################################################################
// ENDGAME B.3.5 block GC item 4 -- THE FORWARDING DIRECTION IS THE NORMAL ONE NOW.
//
// All three classes this file serves are real C++ classes as of B.3.5
// (`Graphics::AnimationEvent` block H1, `Graphics::MaterialInstance` block G,
// `Graphics::Particle` block H2). Until this commit the BODIES lived in the
// `extern "C"` doors below and the MEMBERS forwarded INTO them -- five members
// across `animation.cpp` and `wave27_lane_gfxpart.cpp` spelled
// `return RtGraphics_X_TypeName(this);`. That is the inverted direction: a C++
// call went member -> free function -> node read, so the member was not the body
// and a reader could not tell which spelling was authoritative.
//
// Each GetFileVersion member IS the body (below, in this file's `namespace Graphics`
// block); Type / TypeName / TypeId are DECLARE_TYPE's bodies (object/TypeInfo.h).
//
// `MaterialInstance::Type`, `::TypeName` and `::GetFileVersion` are slots 2, 1
// and 3 of `??_7MaterialInstance@Graphics@@6B@`: Type and TypeName are
// DECLARE_TYPE's bodies, GetFileVersion is defined below.
//
// `this` IS READ BY NONE OF THE EIGHT, and the member spelling does not change
// that: the encodings in this file's banner have no RCX operand at all. The
// members are `const` and touch no field.
// ############################################################################

// ENDGAME B.3.9 block GFX: MaterialInstance's three slot doors (Type,
// TypeName, GetFileVersion) are retired with the minted table, their only
// reader; the members below are the bodies.


// ############################################################################
// ENDGAME B.3.5 step 5, block GC -- THE TWO GetFileVersion BODIES, AS MEMBERS.
//
// Their own `namespace Graphics { }` block (B34_BRIEFS A.2). The byte decode for
// every one of the eight is in this file's top banner.
// ############################################################################

namespace Graphics {

// ---- ??_7AnimationEvent@Graphics@@6B@  slots 2 and 1 -----------------------
// [2] ?Type@     0x1800116d0  `MOV EAX,[?_typeInfo@AnimationEvent@ + 0]; RET`
// [1] ?TypeName@ 0x1800116f0  `MOV RAX,[?_typeInfo@AnimationEvent@ + 8]; RET`
//     `BaseTypeInfo::name` is CONTENT-only, never compared by pointer.
// ENDGAME B.3.9 TI-REST: Type / TypeName are DECLARE_TYPE's bodies over the class's own
// record (graphics_type_records.cpp).
// ?TypeId@AnimationEvent@Graphics@@SAIXZ 0x1800116e0 -- the static twin of slot
// 2, the same read: DECLARE_TYPE's TypeId over the class's own record. The two
// slot doors are retired with the minted vtable; their only reader was that table.

// ---- ??_7MaterialInstance@Graphics@@6B@  slots 2, 1 and 3 ------------------
// [2] ?Type@     0x1800027f0
// [1] ?TypeName@ 0x180002810
// ENDGAME B.3.9 TI-REST: Type / TypeName (and the static TypeId 0x180002800) are DECLARE_TYPE's bodies over the class's own
// record (graphics_type_records.cpp).

// [3] ?GetFileVersion@ 0x180103a60 -- `MOV EAX,4 ; RET`. A LITERAL, not a read:
// the on-disk serialised format version of a Graphics::MaterialInstance.
// Faithful means returning 4, NOT deriving it from anything in our tree -- a
// derived spelling would silently track OUR format if this class's Serialize is
// ever revised, and retail's does not.
int32_t MaterialInstance::GetFileVersion() const { return 4; }

// ---- ??_7Particle@Graphics@@6B@  slots 2, 1 and 3 --------------------------
// [2] ?Type@     0x18001ff10
// [1] ?TypeName@ 0x18001ff30
// ENDGAME B.3.9 TI-REST: Type / TypeName are DECLARE_TYPE's bodies over the class's own
// record (graphics_type_records.cpp).
// ?TypeId@Particle@Graphics@@SAIXZ 0x18001ff20 -- the static twin of slot 2:
// DECLARE_TYPE's TypeId over the class's own record.

// [3] ?GetFileVersion@ 0x180104080 -- `MOV EAX,0x1000 ; RET`, 4096, and it is NOT
// the same constant as MaterialInstance's 4. The two were decoded separately and
// that difference is why each is written as its own literal.
int32_t Particle::GetFileVersion() const { return 0x1000; }

}  // namespace Graphics
