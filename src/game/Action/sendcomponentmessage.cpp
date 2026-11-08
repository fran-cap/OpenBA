// src/game/Action/sendcomponentmessage.cpp
// Clean C++ reimplementation of SendComponentMessage action methods.
// Source: Game/Action/SendComponentMessage.cpp (from profile build debug strings)
//
// SendComponentMessage — 0x40 bytes. Sends a message to a specific component
// type on the target entity.
// Inherits: Object::Internal -> Action (0x20) -> SendComponentMessage
// Ctor: 0x140023070 (factory), Vtable: 0x1401600c0
//
// Decompiled from Application-x64.exe using Ghidra.

#include "SendComponentMessage.h"
#include "object/TypeInfo.h"
#include "object/Type.h"   // RtObject::RtObject_Type_Serialize
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdint>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/Component.h"          // ENDGAME B.3.8 CB3: slot 15 OnMessage
#include "Entity/ComponentEntity.h"       // ENDGAME B.3.9 C4: ComponentEntity::TypeId()

// ============================================================
// Helpers, called by name (B3R)
//   import slot 0x1401516b0 ?Serialize@Type@Object@@QEAAXAEAVSerializer@IO@@@Z ->
//               RtObject::RtObject_Type_Serialize (object/Type.h)
// ============================================================

// ============================================================
// Serialize (0x1400cb000, vtable slot 3)
// Serializes status, two entity refs with type checks, component type,
// message ID, and two uint params.
// ============================================================
void SendComponentMessage::Persist(IO::Stream& stream) {
    Action::Persist(stream);   // inlined in retail: Serializer(stream, 0), status +0x18

    // Serialize entity refs + message fields (version 0)
    IO::Serializer ser(stream, 0);

    // Target entity ref (+0x20) with type validation
    _target.InternalBasePtr::Serialize(ser);
    if (!_target.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(_target, ComponentEntity::TypeId())) {
            _target = nullptr;
        }
    }

    // Source entity ref (+0x28) with type validation
    _sender.InternalBasePtr::Serialize(ser);
    if (!_sender.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(_sender, ComponentEntity::TypeId())) {
            _sender = nullptr;
        }
    }

    // Component type (+0x30) — Object::Type::Serialize
    RtObject::RtObject_Type_Serialize(&_componentTypeId, &ser);   // slot 0x1401516b0

    ser.Serialize(_message, nullptr);   // +0x34 message id (int)
    ser.Serialize(_messageArg, nullptr);   // +0x38 message param (uint)
    ser.Serialize(_secondMessageArg, nullptr);   // +0x3C message param (uint)
}

// ============================================================
// AbstractImpl (0x1400caef0, vtable slot 4)
// Finds component by type ID on target entity, then calls its
// message handler virtual method (vtable+0x78).
// Returns 0 on success, 2 if component not found.
// ============================================================
int  SendComponentMessage::AbstractImpl(ActionStack* actionStack) {
    (void)actionStack;

    uint32_t componentTypeId = _componentTypeId;

    // 0x1400caf1a CheckPtr(+0x20); 0x1400caf20-0x1400caf57: the target's component array
    // (+0x30 data, +0x38 count) scanned by slot 2 (Type) with no test of the target, of a
    // slot or of the entity's flags; the first match is taken (0x1400caf95).
    // (The tree's canonical-pointer / readable-memory / marked-for-removal probes of the
    // target and its sentinel-slot skip are gone, B6 WORK G72: the weak-ref invalidation
    // reason is retired with PlayAnimation's, since B.4 gave every class its ctor. If a
    // message reaches a freed or dying entity again, that is an upstream finding: the
    // action should not outlive its target.)
    ComponentEntity* target = _target.Get();
    int32_t compCount = target->_components._size;
    Object::InternalPtr<Component>* compArray = target->_components._data;
    Component* foundComp = nullptr;

    for (int i = 0; i < compCount; i++) {
        Component* comp = compArray[i].Get();
        int typeId = static_cast<int>(comp->Type());   // slot 2
        if (typeId == (int)componentTypeId) {
            foundComp = compArray[i].Get();
            break;
        }
    }

    if (foundComp == nullptr) {
        return 2;
    }

    // Call the component's message handler (slot 15, vtable+0x78):
    // component, msgId, &sourceEntity, param1, param2
    ComponentEntity* sourceEntityRef = _sender.Get();
    foundComp->OnMessage(_message, &sourceEntityRef, _messageArg, _secondMessageArg);

    return 0;
}

// ============================================================
// GetTarget (0x1400cae80, vtable slot 9)
// Returns true if param matches entity at +0x20 or +0x28.
// ============================================================
bool SendComponentMessage::GetTarget(ComponentEntity** param) {
    // CheckPtr on +0x20, then compare [param] directly (no CheckPtr on param)
    if (*param == _target.Get()) {
        return true;
    }

    // CheckPtr on +0x28, then compare [param]
    return *param == _sender.Get();
}

// ============================================================
// class SendComponentMessage (SendComponentMessage.h) — ENDGAME B.3.8 ACT.
// The hand-built g_SendComponentMessage_Vtable[10], its per-class type-id cache and the
// Action_Alloc / Action_InitInPlace factories are gone: the compiler emits
// the table, DECLARE_EXE_TYPE answers slots 1/2.
// ============================================================
#include "Object/factory_registration.h"

// create 0x140023070. Retail's default ctor (inlined, 0x1400230cc) stores +0x34 = -1;
// the tree's factory zero-filled every field instead. B38 B8-1: the retail stores are kept; the
// fields retail leaves unwritten stay zero-filled (MemAlloc gives no guarantee).
SendComponentMessage::SendComponentMessage()
    : _target(nullptr),
      _sender(nullptr),
      _componentTypeId(0),
      _message(-1),
      _messageArg(0),
      _secondMessageArg(0) {}

// [3] 0x1400cb000
// [4] 0x1400caef0
// [9] 0x1400cae80

::Object::Base* SendComponentMessage::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(SendComponentMessage), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) SendComponentMessage());
}
::Object::Base* SendComponentMessage::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) SendComponentMessage());
}


// 0x1400cb1d0 — the ctor body: Action's base stores, then +0x20 = *param_2,
// +0x28 = *param_5, +0x30 = *param_3 as given (0x1400cb23f), +0x34 = param_4,
// +0x38 = param_6, +0x3c = 0. Every sender hands its component's TypeId() (row 1294).
SendComponentMessage::SendComponentMessage(ComponentEntity* const* param_2, const uint32_t* param_3,
                                           uint32_t param_4, ComponentEntity* const* param_5,
                                           uint32_t param_6)
    : _target(*param_2),
      _sender(*param_5) {
    _componentTypeId = *param_3;
    _message = static_cast<int32_t>(param_4);
    _messageArg = param_6;
    _secondMessageArg = 0;
}

DEFINE_EXE_TYPE(SendComponentMessage, "Action")
