// src/game/Component/charactercomponent_serialize.cpp
// Slot-3 (+0x18) stream Serialize for CharacterComponent.
//
//   CharacterComponent::Serialize   retail 0x140109ab0   object size 0x80
//
// Save/load wave (2026-08-27). Retail body (byte-faithful, from decompile):
//   Component_Serialize(self, stream);
//   Serializer ser(stream, /*version*/0);
//   Vector3::Serialize(self+0x2c, ser);                  // world position (12 bytes)
//   Serialize_Float(ser, self+0x3c);                     // (order per retail: 0x3c before 0x38)
//   Serialize_Float(ser, self+0x38);
//   Serialize_Bool (ser, self+0x28);
//   Serialize_Int  (ser, self+0x60);                     // action-stack top
//   count = self+0x50; Serialize_Int(ser, &count);
//   if (loading) ResizeIBPArray(self+0x48, count);        // action-stack array (8-byte IBPs)
//   for i in [0,count): IBP::Serialize(&arr[i], ser); validate Action, null on mismatch.
//   IBP::Serialize(self+0x58, ser); validate Action, null on mismatch.
//   IBP::Serialize(self+0x68, ser); validate ModelComponent (0x1401c7a50), null on mismatch.
//   IBP::Serialize(self+0x70, ser); validate PickingComponent, null on mismatch.
//   if (loading) {
//       obj = self+0x18 (owner entity backref);
//       self+0x78 = IsDerivedFrom(obj, CharacterDescription/0x1401c70b0) ? obj : null;
//       ibp = InternalBasePtr(self);
//       self+0x40 = CharMgr_sub_83f0(gameWorld+0x88, &ibp);   // re-register char index
//   }
//
// This mirrors CharacterComponent_BodyInit's own field derivation
// (charactercomponent.cpp) — on load retail re-derives +0x78/+0x40 exactly the
// same way Startup does, so we reuse CharMgr_sub_83f0 (already a real C++
// function, charmanager.cpp) rather than re-deriving it.
//
// Sub-callees: all through already-resolved engine_funcs.h globals
// (g_fn_Serializer_Ctor/Dtor/GetVersion is NOT used here — version is fixed 0
// and retail never re-reads it) plus Math::Vector3::Serialize,
// fn_Serializer_Serialize_Float, g_fn_Serialize_Bool, g_fn_Serialize_Int,
// fn_InternalBasePtr_Serialize, fn_IsDerivedFrom, fn_InternalBasePtr_Ctor, and
// the array-resize helper FUN_1400032e0 (already ported/registered elsewhere —
// see animalwave_serialize.cpp's s_Resize8 use of the same address).
// The base serialize (0x1401189b0) is Component::Persist, called qualified.
//
// Validation type ids resolved locally (self-contained, matching the house
// pattern in animalwave_serialize.cpp): 0x1401c7a50 = ModelComponent,
// g_BaseTypeInfo_PickingComponent's raw slot, Action's cell (Action::TypeId()), and
// 0x1401c70b0 = CharacterDescription. These are the SAME raw .rdata addresses
// already resolved (under different local names) by other files
// (action_pf_ctors.cpp, boatcomponent_route.cpp, animalcomponent.cpp) — we
// resolve our own copies rather than share their statics, per house convention.

#include "math/Math.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/CharacterComponent.h"
#include "Component/CharacterGrid.h"
#include "Component/ModelComponent.h"        // ENDGAME B.3.9 C1: 0x1401c7a50 cell class
#include "Component/PickingComponent.h"      // ENDGAME B.3.9 C1: 0x1401c7c10 cell class
#include "Component/CharacterDescription.h"  // ENDGAME B.3.9 C1: 0x1401c70b0 cell class
#include "Action/Action.h"                   // ENDGAME B.3.9 RF: 0x1401c5da8 cell class


// FUN_1400032e0 was Collection::Array<void*>::SetSize(n) monomorphised into
// the exe (ENDGAME B.3.2 step 2) — converted at its call site below.

namespace {

// ENDGAME B.3.9 C1 (B39_RULES.md D5): 0x1401c7a50 (ModelComponent), 0x1401c7c10
// (PickingComponent) and 0x1401c70b0 (CharacterDescription) are registered
// type-id cells, read BY CELL as the class's static TypeId(). 0x1401c5da8 is the
// ABSTRACT base Action's cell: Action::TypeId() since ENDGAME B.3.9 RF gave the
// abstract bases their records.

// Serialize one InternalPtr<T> member through its InternalBasePtr and null it when
// the pointee is not derived from `tid`.
template <class T>
void SerializeValidatedIBP(Object::InternalPtr<T>& field, IO::Serializer& ser, uint32_t tid) {
    field.InternalBasePtr::Serialize(ser);
    if (field && tid && !::Object::IsDerivedFromNullSafe(field, tid))
        field = nullptr;
}

} // namespace

// [3] 0x140109ab0.
void CharacterComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail's direct CALL 0x1401189b0

    IO::Serializer ser(stream, 0);

    // ?Serialize@Vector3@Math@@QEAAXAEAVSerializer@IO@@@Z  retail 0x1800150e0
    moveDirection.Serialize(ser);
    ser.Serialize(targetHeading, nullptr);
    ser.Serialize(moveSpeedScale, nullptr);
    ser.Serialize(steeredByMover, nullptr);
    ser.Serialize(actionStack.current, nullptr);

    int count = actionStack.actions._size;
    ser.Serialize(count, nullptr);
    // exe 0x1400032e0 IS Collection::Array<InternalPtr>::SetSize(n) (kSpec_PtrZero).
    const bool loading = static_cast<int>(ser.GetDirection()) == 0;   // the stream's +8 word
    if (loading) actionStack.actions.SetSize(count);

    const uint32_t tidAction = Action::TypeId();   // cell 0x1401c5da8
    ::Object::InternalPtr<Action>* member = actionStack.actions._data;
    for (int i = 0; i < count && member; ++i)
        SerializeValidatedIBP(member[i], ser, tidAction);

    SerializeValidatedIBP(actionStack.completionAction, ser, tidAction);
    SerializeValidatedIBP(modelComponent, ser, ModelComponent::TypeId());
    SerializeValidatedIBP(pickingComponent, ser, PickingComponent::TypeId());

    if (static_cast<int>(ser.GetDirection()) == 0) {
        Description* owner = _description;
        bool isDesc = owner && ::Object::IsDerivedFromNullSafe(owner, CharacterDescription::TypeId());
        characterDescription = isDesc ? static_cast<CharacterDescription*>(owner) : nullptr;

        {
            // GameUpdateLoop::m_characterGrid (+0x88)
            CharacterGrid* charMgr = GameUpdateLoop::_singleton
                                         ? GameUpdateLoop::_singleton->m_characterGrid
                                         : nullptr;
            if (charMgr) {
                Object::InternalPtr<CharacterComponent> ibpArg(this);
                characterIndex = static_cast<int32_t>(charMgr->Register(&ibpArg));
            }
        }
    }
}
