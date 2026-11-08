// src/game/Action/removenaturalresource.cpp
// Clean C++ reimplementation of RemoveNaturalResource action methods.
// Source: Game/Action/RemoveNaturalResource.cpp (from profile build debug strings)
//
// RemoveNaturalResource — 0x38 bytes. Removes a natural resource entity.
// Inherits: Object::Internal -> Action (0x20) -> RemoveNaturalResource
// Factory: 0x140022f70, Vtable: 0x140160050
//
// Vtable (10 slots):
//   [0] +0x00 ScalarDtor    = 0x140022fe0
//   [1] +0x08 VectorDtor    = 0x140022f60 (returns type ptr, unhookable)
//   [2] +0x10 BaseDtor      = 0x140022f50 (returns TypeInfo, unhookable)
//   [3] +0x18 Serialize     = 0x1400cac50
//   [4] +0x20 AbstractImpl  = 0x1400cab10
//   [5] +0x28 Execute       = 0x1400c4a90 (base Action, returns 0)
//   [6] +0x30 Enter         = 0x1400c4b90 (base Action, void nop)
//   [7] +0x38 Exit          = 0x1400c4ba0 (base Action, void nop)
//   [8] +0x40 CheckEntity   = 0x1400caaa0
//   [9] +0x48 GetTarget     = 0x1400caa20
//
// Field layout (0x38 bytes):
//   +0x00-0x1F: Action base (vtable, Internal base, entity ptr, status byte)
//   +0x20: void*    citizenEntity  (InternalBasePtr, type DAT_1401c7158)
//   +0x28: void*    resourceEntity (InternalBasePtr, ComponentEntity type)
//   +0x30: uint8_t  harvestFlag
//
// Decompiled from Application-x64.exe using Ghidra.

#include "RemoveNaturalResource.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Action/exe_bodies.h"   // RemoveNaturalResourceCore
#include "Component/Description.h"
#include "Component/CitizenDescription.h"
#include "Component/NaturalResourceComponent.h"
#include "Game/ProfessionList.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G
#include "Component/CitizenComponent.h"
#include "Component/NaturalResourceDescription.h"
#include "Entity/ComponentEntity.h"

// ============================================================
// Helpers, called by name (B3R)
//   0x1400052c0 WorkBase_FindComponent_52c0 -- FindComponent by type DAT_1401c7b30
//               (NaturalResourceComponent); RCX=entity, RDX=outIBP
//   0x1400c3470 ActHunt_NotifyEntityChanged_c3470 -- notify entity changed (visual update)
//   0x1401260f0 ActCNR_RemoveNatRes_1260f0 -- remove natural resource helper
//               (natResComp, citizenEntity IBP, harvestFlag)
//
// ENDGAME B.3.9 CB: retail reads this record field ONLY in the class's own TypeName
// slot (0x1401c60c0 = the NAME field (+8) of RemoveNaturalResource's record, read by
// RemoveNaturalResource_Slot1 0x140022f60); DECLARE_EXE_TYPE answers that slot, so the tree's copy of it is gone.
// ============================================================

// ============================================================
// Serialize (0x1400cac50)
// Serializes: status(+0x18), entity(+0x20) with type check, entity(+0x28) with type check, bool(+0x30)
// ============================================================
void RemoveNaturalResource::Persist(IO::Stream& stream) {
    Action::Persist(stream);   // inlined in retail: Serializer(stream, 0), status +0x18

    // Serialize entity refs and harvest flag
    IO::Serializer ser(stream, 0);

    // The working citizen (+0x20)
    _citizen.InternalBasePtr::Serialize(ser);
    if (!_citizen.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(_citizen, CitizenComponent::TypeId())) {
            _citizen = nullptr;
        }
    }

    // The natural-resource entity (+0x28)
    _resource.InternalBasePtr::Serialize(ser);
    if (!_resource.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(_resource, ComponentEntity::TypeId())) {
            _resource = nullptr;
        }
    }

    // Serialize harvest flag (+0x30)
    ser.Serialize(_harvestFlag, nullptr);
}

// ============================================================
// AbstractImpl (0x1400cab10) — slot 4
// Checks the resource's profession, sets its mature / harvestable flags, calls the
// removal helper (0x1401260f0). Returns 0 (action complete).
// ============================================================
int  RemoveNaturalResource::AbstractImpl(ActionStack* actionStack) {
    (void)actionStack;

    // The resource entity's NaturalResourceComponent (+0x28)
    NaturalResourceComponent* nrComp = _resource->FindComponent<NaturalResourceComponent>();   // 0x1400052c0

    // B6 (14d G71): 0x1400cab10 reads the found component's +0x18 with no test (CheckPtr,
    // then obj = [local_res8+0x18]); the old guard's cause, the swapped
    // RemoveNaturalResource ctor arguments in 0x1400c1eb0's port, is fixed there.
    Description* descObj = nrComp->_description;

    // Check if description is NaturalResourceDescription type
    bool isNatResDesc = descObj->IsDerivedFrom(NaturalResourceDescription::TypeId());   // cell 0x1401c7b68
    const NaturalResourceDescription* natResDesc =
        isNatResDesc ? static_cast<const NaturalResourceDescription*>(descObj) : nullptr;

    // The resource's profession, against the town's default (ProfessionList +0x30)
    void* ownerRef = natResDesc->_profession;
    Profession* townOwner = GameUpdateLoop::_singleton->m_professionList->laborerProfession;

    if (ownerRef != nullptr && ownerRef != townOwner) {
        // Not harvestable unless the citizen has the resource's profession
        if (_citizen->profession != ownerRef) {
            nrComp->harvestable = false;
        }
    }

    // Mature when the citizen's education has reached its description's maximum
    CitizenComponent* citizen = _citizen.Get();
    int threshold = citizen->description->maxEducation;
    int growthValue = citizen->education;

    nrComp->mature = (threshold <= growthValue);

    // The citizen's workplace (+0x248)
    ComponentEntity* workEntityIBP = _citizen->workplace.Get();

    // Call the removal helper
    nrComp->RemoveResource(&workEntityIBP, _harvestFlag);   // 0x1401260f0

    return 0;
}

// ============================================================
// CheckEntity (0x1400caaa0) — slot 8
// Clears the "in use" bit (bit 20) of the resource's NaturalResourceComponent flags.
// ============================================================
void RemoveNaturalResource::CheckEntity() {
    NaturalResourceComponent* nrComp = _resource->FindComponent<NaturalResourceComponent>();   // 0x1400052c0

    // B6 (14d G71): 0x1400caaa0 reads [comp+0x20] with no test.
    uint32_t flagsOld = nrComp->_flags;
    uint32_t flagsNew = flagsOld & 0xFFEFFFFF;  // clear bit 20
    nrComp->_flags = flagsNew;

    if (((flagsNew ^ flagsOld) & 0xFFFF) != 0) {
        ComponentEntity* parent = nrComp->_entity.Get();
        if (parent != nullptr) {
            nrComp->_entity.Get()->RefreshEntityFlags();   // 0x1400c3470
        }
    }
}

// ============================================================
// GetTarget (0x1400caa20) — slot 9
// Returns true if param matches the resource entity at +0x28 or the citizen's (+0x20)
// owning entity (Component +0x10).
// ============================================================
bool RemoveNaturalResource::GetTarget(ComponentEntity** param) {
    void* target = *param;

    if (target == _resource.Get()) {
        return true;
    }

    return _citizen->_entity.Get() == target;
}

// ============================================================
// class RemoveNaturalResource (RemoveNaturalResource.h) — ENDGAME B.3.8 ACT.
// The hand-built g_RemoveNaturalResource_Vtable[10], its per-class type-id cache and the
// Action_Alloc / Action_InitInPlace factories are gone: the compiler emits
// the table, DECLARE_EXE_TYPE answers slots 1/2.
// ============================================================
#include "Object/factory_registration.h"

// create 0x140022f70 (inlined ctor). The tree's factory zeroed every field above
// +0x10; kept (not a behaviour change of this step).
RemoveNaturalResource::RemoveNaturalResource()
    : _citizen(nullptr),
      _resource(nullptr),
      _harvestFlag(false),
      _pad31{} {}

// [3] 0x1400cac50
// [4] 0x1400cab10
// [8] 0x1400caaa0
// [9] 0x1400caa20

::Object::Base* RemoveNaturalResource::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(RemoveNaturalResource), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) RemoveNaturalResource());
}
::Object::Base* RemoveNaturalResource::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) RemoveNaturalResource());
}


// ============================================================
// FUN_1400cadf0 — RemoveNaturalResource_Action_CopyCtor
//   RCX=self, RDX=param_2, R8=param_3, R9B=param_4 (byte @ +0x30).
//   +0x20 = *param_3 ; +0x28 = *param_2 ; +0x30 = param_4.
// ============================================================
// 0x1400cadf0: +0x20 = *param_3 ; +0x28 = *param_2 ; +0x30 = param_4.
RemoveNaturalResource::RemoveNaturalResource(ComponentEntity* const* resource, CitizenComponent* const* citizen,
                                             bool flag)
    : _citizen(*citizen),
      _resource(*resource),
      _harvestFlag(flag) {}

DEFINE_EXE_TYPE(RemoveNaturalResource, "Action")
