#pragma once
// ============================================================================
// src/game/Action/MoveTo.h — ENDGAME B.3.8 block ACT (B38_RULES.md D1-D5)
//
// `MoveTo`, an exe Action leaf, as a real C++ class with a compiler vtable.
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "MoveTo", parent "Action", ti 0x1401c5fa0
//   create 0x1400228c0: MemAlloc(0x480, 8) + the inlined ctor
//   createInPlace = the IMPORTED `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and that is KEPT (B8-13).
//   table 0x14015fe40, 10 slots; every slot not declared below is Action's (= parent)
//
// FIELDS (B.4 ACT-b): from the ctor 0x1400c9170, Persist 0x1400c8ec0 and the slot
// bodies; the path setup 0x1400c8c10 and the waypoint facing 0x1400c8d20
// (action_base_move.cpp, ACT-a's file) still read them at raw offsets.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Action/Action.h"
#include "Action/PathSearch.h"   // PathTicket
#include "Game/PathType.h"       // PathBits (the ctor's pass flags)
#include "Object/exe_type.h"
#include "object/Internal.h"     // Object::InternalPtr<T>
#include "math/Math.h"           // Math::Vector3

class CharacterComponent;     // Component/CharacterComponent.h
class CharacterDescription;   // Component/CharacterDescription.h

class MoveTo final : public Action {
    DECLARE_EXE_TYPE(MoveTo)

public:
    MoveTo();                                                   // create 0x1400228c0
    MoveTo(CharacterComponent* const* character, uint32_t moveAnimation, int stopAnimation,
           const float* destination, PathBits passFlags, int movementProfile,
           float speed);   // 0x1400c9170
    ~MoveTo() override = default;                              // [0] 0x140022900
    void Persist(IO::Stream& stream) override;                       // [3] 0x1400c8ec0
    int  AbstractImpl(ActionStack* actionStack) override;             // [4] 0x1400c8ab0
    int  Execute(ActionStack* actionStack, const System::Time* time) override;      // [5] 0x1400c8570
    void Enter(ActionStack* actionStack, const System::Time* time) override;        // [6] 0x1400c8970
    void Exit(ActionStack* actionStack) override;                     // [7] 0x1400c8520
    void CheckEntity() override;                               // [8] 0x1400c84d0
    bool GetTarget(ComponentEntity** param) override;                      // [9] 0x1400c8480

    void UpdateFacing();                                                   // 0x1400c8d20 (body in action_base_move.cpp)
    // Retail takes RCX / RDX only; the caller's R8 / R9 are stale registers (the Vector2 of the
    // distance test, the ActionStack), modelled as two unread parameters (body in action_base_move.cpp).
    bool PathSetup(const float* worldPos);   // 0x1400c8c10 (retail returns AL = 1)

    // ---- fields, retail order (PUBLIC: 0x1400c8c10 / 0x1400c8d20 read them) ----
    PathTicket ticket;           // +0x20 the pathfinder ticket {slot (-1 = none), token}
                                 //       (0x1400c8c10 stores RequestPath's result here)
    Object::InternalPtr<CharacterComponent> character;   // +0x28 the moving character
                                 //       (Persist checks CharacterComponent)
    CharacterDescription* description;   // +0x30 ExternalPtr<CharacterDescription>: the
                                 //       character's description when it is one (ctor)
    Math::Vector3 destination;   // +0x38 Persist: Vector3::Serialize
    // +0x44..+0x5B: the ctor zeroes them with three qword stores (0x44, 0x4C, 0x54);
    // no slot body reads them.
    int32_t   field_0x44;        // +0x44
    int32_t   field_0x48;        // +0x48
    int32_t   field_0x4C;        // +0x4C
    int32_t   field_0x50;        // +0x50
    int32_t   field_0x54;        // +0x54
    int32_t   field_0x58;        // +0x58
    uint32_t  passFlags;         // +0x5C RequestPath's pass flags (0x1400c8c10); Persist: uint
    int32_t   pathIndex;         // +0x60 the waypoint being walked to (-1 = no path yet)
    int32_t   pathCount;         // +0x64 cells in `path`
    int32_t   path[0x100];       // +0x68 the path's cells (TakePath 0x1400a6d50, max 0x100)
    int32_t   moveAnimation;     // +0x468 played when a path starts (Persist: int)
    int32_t   stopAnimation;     // +0x46C played when the path ends / on Exit (Persist: int)
    int32_t   movementProfile;   // +0x470 SetMovementCosts' profile (0x1400c8c10; Persist: int)
    bool      field_0x474;       // +0x474 set by Enter when the nearest character faces our
                                 //        way; Execute then walks at description->_f30
    uint8_t   _pad475[3];        // +0x475 padding
    float     speedMultiplier;   // +0x478 walk speed
    uint8_t   _pad47C[4];        // +0x47C padding
};

static_assert(sizeof(MoveTo) == 0x480, "MoveTo is 0x480 (create 0x1400228c0 MemAlloc)");
static_assert(offsetof(MoveTo, _pad475) == 0x475, "MoveTo::_pad475");
static_assert(offsetof(MoveTo, _pad47C) == 0x47C, "MoveTo::_pad47C");
static_assert(alignof(MoveTo) == 8, "MoveTo is 8-aligned");
static_assert(offsetof(MoveTo, ticket) == 0x20, "MoveTo +0x20");
static_assert(offsetof(MoveTo, character) == 0x28, "MoveTo +0x28");
static_assert(offsetof(MoveTo, description) == 0x30, "MoveTo +0x30");
static_assert(offsetof(MoveTo, destination) == 0x38, "MoveTo +0x38");
static_assert(offsetof(MoveTo, field_0x44) == 0x44, "MoveTo +0x44");
static_assert(offsetof(MoveTo, field_0x48) == 0x48, "MoveTo +0x48");
static_assert(offsetof(MoveTo, field_0x4C) == 0x4C, "MoveTo +0x4C");
static_assert(offsetof(MoveTo, field_0x50) == 0x50, "MoveTo +0x50");
static_assert(offsetof(MoveTo, field_0x54) == 0x54, "MoveTo +0x54");
static_assert(offsetof(MoveTo, field_0x58) == 0x58, "MoveTo +0x58");
static_assert(offsetof(MoveTo, passFlags) == 0x5C, "MoveTo +0x5C");
static_assert(offsetof(MoveTo, pathIndex) == 0x60, "MoveTo +0x60");
static_assert(offsetof(MoveTo, pathCount) == 0x64, "MoveTo +0x64");
static_assert(offsetof(MoveTo, path) == 0x68, "MoveTo +0x68");
static_assert(offsetof(MoveTo, moveAnimation) == 0x468, "MoveTo +0x468");
static_assert(offsetof(MoveTo, stopAnimation) == 0x46C, "MoveTo +0x46C");
static_assert(offsetof(MoveTo, movementProfile) == 0x470, "MoveTo +0x470");
static_assert(offsetof(MoveTo, field_0x474) == 0x474, "MoveTo +0x474");
static_assert(offsetof(MoveTo, speedMultiplier) == 0x478, "MoveTo +0x478");
