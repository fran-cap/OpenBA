#pragma once
// ============================================================================
// src/game/Action/HuntAnimal.h — ENDGAME B.3.8 block ACT (B38_RULES.md D1-D5)
//
// `HuntAnimal`, an exe Action leaf, as a real C++ class with a compiler vtable.
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "HuntAnimal", parent "Action", ti 0x1401c5f68
//   create 0x140022700: MemAlloc(0x40, 8) + the inlined ctor
//   createInPlace = the IMPORTED `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and that is KEPT (B8-13).
//   table 0x14015fde0, 10 slots; every slot not declared below is Action's (= parent)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Action/Action.h"
#include "Object/exe_type.h"
#include "object/Internal.h"   // Object::InternalPtr<T>

class CitizenComponent;   // Component/CitizenComponent.h
class HuntComponent;      // Component/HuntComponent.h

class HuntAnimal final : public Action {
    DECLARE_EXE_TYPE(HuntAnimal)

public:
    HuntAnimal();                                                   // create 0x140022700
    HuntAnimal(CitizenComponent* const* hunter, HuntComponent* const* hunt);   // 0x1400c8400
    ~HuntAnimal() override = default;                          // [0] 0x140022770
    void Persist(IO::Stream& stream) override;                       // [3] 0x1400c8260
    int  AbstractImpl(ActionStack* actionStack) override;             // [4] 0x1400c8250
    int  Execute(ActionStack* actionStack, const System::Time* time) override;      // [5] 0x1400c8240
    void Enter(ActionStack* actionStack, const System::Time* time) override;        // [6] 0x1400c7de0
    bool GetTarget(ComponentEntity** param) override;                      // [9] 0x1400c7d60

    // ---- fields, retail order (PUBLIC: the slot bodies are C functions) ----
    // +0x20: the HuntComponent (Persist checks HuntComponent; Enter tests HuntIsReady and clears
    // its huntProgress). +0x28: the HUNTING CITIZEN (Persist checks CitizenComponent; its
    // character's position seeds the query). The tree called them "animal" and "herd".
    Object::InternalPtr<HuntComponent>    _hunt;
    Object::InternalPtr<CitizenComponent> _hunter;
    void*     _unreadZeroedPointer;          // +0x30 ctor 0; read by no slot
    bool      _huntStarted;          // +0x38 hunt started flag (Persist: the bool overload)
    uint8_t   _pad39[7];         // +0x39 padding
};

static_assert(sizeof(HuntAnimal) == 0x40, "HuntAnimal is 0x40 (create 0x140022700 MemAlloc)");
static_assert(offsetof(HuntAnimal, _pad39) == 0x39, "HuntAnimal::_pad39");
static_assert(alignof(HuntAnimal) == 8, "HuntAnimal is 8-aligned");
static_assert(offsetof(HuntAnimal, _hunt) == 0x20, "HuntAnimal +0x20");
static_assert(offsetof(HuntAnimal, _hunter) == 0x28, "HuntAnimal +0x28");
static_assert(offsetof(HuntAnimal, _unreadZeroedPointer) == 0x30, "HuntAnimal +0x30");
static_assert(offsetof(HuntAnimal, _huntStarted) == 0x38, "HuntAnimal +0x38");
