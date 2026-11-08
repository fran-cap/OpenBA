#pragma once
// ============================================================================
// src/game/Action/SendComponentMessage.h — ENDGAME B.3.8 block ACT (B38_RULES.md D1-D5)
//
// `SendComponentMessage`, an exe Action leaf, as a real C++ class with a compiler vtable.
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "SendComponentMessage", parent "Action", ti 0x1401c60f0
//   create 0x140023070: MemAlloc(0x40, 8) + the inlined ctor
//   createInPlace = the IMPORTED `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and that is KEPT (B8-13).
//   table 0x1401600c0, 10 slots; every slot not declared below is Action's (= parent)
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Action/Action.h"
#include "Object/exe_type.h"
#include "object/Internal.h"   // Object::InternalPtr<T>

class ComponentEntity;   // Entity/ComponentEntity.h

class SendComponentMessage final : public Action {
    DECLARE_EXE_TYPE(SendComponentMessage)

public:
    SendComponentMessage();                                                   // create 0x140023070
    SendComponentMessage(ComponentEntity* const* target, const uint32_t* compTypeId, uint32_t message,
                         ComponentEntity* const* second, uint32_t arg);   // 0x1400cb1d0
    ~SendComponentMessage() override = default;                // [0] 0x1400230f0
    void Persist(IO::Stream& stream) override;                       // [3] 0x1400cb000
    int  AbstractImpl(ActionStack* actionStack) override;             // [4] 0x1400caef0
    bool GetTarget(ComponentEntity** param) override;                      // [9] 0x1400cae80

    // ---- fields, retail order (PUBLIC: the slot bodies are C functions) ----
    Object::InternalPtr<ComponentEntity> _target;   // +0x20 target entity (Persist checks ComponentEntity)
    Object::InternalPtr<ComponentEntity> _sender;   // +0x28 source entity handed to OnMessage (Persist checks ComponentEntity)
    uint32_t  _componentTypeId;          // +0x30 component type id (Object::Type::Serialize)
    int32_t   _message;          // +0x34 message id (Persist: int)
    uint32_t  _messageArg;          // +0x38 message arg (Persist: uint)
    uint32_t  _secondMessageArg;          // +0x3C second message arg, ctor 0 (Persist: uint)
};

static_assert(offsetof(SendComponentMessage, _secondMessageArg) == 0x3C, "SendComponentMessage +0x3C");

static_assert(sizeof(SendComponentMessage) == 0x40, "SendComponentMessage is 0x40 (create 0x140023070 MemAlloc)");
static_assert(alignof(SendComponentMessage) == 8, "SendComponentMessage is 8-aligned");
static_assert(offsetof(SendComponentMessage, _target) == 0x20, "SendComponentMessage +0x20");
static_assert(offsetof(SendComponentMessage, _sender) == 0x28, "SendComponentMessage +0x28");
static_assert(offsetof(SendComponentMessage, _componentTypeId) == 0x30, "SendComponentMessage +0x30");
static_assert(offsetof(SendComponentMessage, _message) == 0x34, "SendComponentMessage +0x34");
static_assert(offsetof(SendComponentMessage, _messageArg) == 0x38, "SendComponentMessage +0x38");
