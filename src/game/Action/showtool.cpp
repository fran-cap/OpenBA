// src/game/Action/showtool.cpp
// Clean C++ reimplementation of ShowTool action methods.
// Source: Game/Action/ShowTool.cpp (from profile build debug strings)
//
// ShowTool -- 0x30 bytes. Shows a tool UI for a citizen.
// Inherits: Object::Internal -> Action (0x20) -> ShowTool
// Vtable at 0x1401589b0 (10 slots).
//
// Decompiled from Application-x64.exe using Ghidra.

#include "ShowTool.h"
#include "Component/ModelDescription.h"   // ENDGAME B.3.8 CB3: ModelDescription::TypeId()
#include "Component/RawMaterialDescription.h"   // ENDGAME B.3.8 CB3: RawMaterialDescription::TypeId()
#include "Component/CitizenComponent.h"   // ENDGAME B.3.8 CB3: CitizenComponent::TypeId()
// The runtime type headers come FIRST: they reach src/engine/volume/Volume.h,
// whose global-scope `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()`
// once <windows.h> has been seen through resolve.h.  ENDGAME B.3.5 batch K1.
#include "graphics/Geometry.h"
#include "graphics/Mesh.h"
#include "graphics/GraphicsMesh.h"
#include "graphics/Scene.h"
#include "system/Value.h"       // System::Time, for Geometry::Update's argument
#include "object/External.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Entity/exe_bodies.h"      // fn_FindComponent_Storage (the guarded copy)
#include "Component/CharacterComponent.h"
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Component/ModelComponent.h"
#include "Component/StorageComponent.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Action/exe_bodies.h"   // ActCt_HideTool_1176e0, ActCt_ShowToolOnEntity_1173b0
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include "math/Math.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <new>

#include "Object/factory_registration.h"   // ::Object::GetTypeIdByName (SA type-id fallback)
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G

// ============================================================
// Helpers, called by name (B3R)
//   0x1401176e0 ActCt_HideTool_1176e0         -- hide tool on entity
//   0x1401173b0 ActCt_ShowToolOnEntity_1173b0 -- show tool with specific ID on entity
// ============================================================

// DAT_1401c7158 -- TypeInfo for entity type check

// B6 (14d G74): the tree's second, guarded port of 0x1401173b0's toolId < 0 branch
// (ShowTool_ShowCarried and its finder) is gone: AbstractImpl calls ShowCitizenTool
// (action_base_complete.cpp, the one port of 0x1401173b0) for every toolId it shows,
// as retail 0x14004275e does.

// ============================================================
// ShowTool::Serialize (0x140042780)
// Serializes: status(+0x18), citizen(+0x20) with type check, int(+0x28)
// ============================================================
void ShowTool::Persist(IO::Stream& stream) {
    Action::Persist(stream);   // inlined in retail: Serializer(stream, 0), status +0x18

    IO::Serializer ser(stream, 0);
    _citizen.InternalBasePtr::Serialize(ser);
    if (!_citizen.IsNull()) {
        if (!::Object::IsDerivedFromNullSafe(_citizen, CitizenComponent::TypeId())) {   // cell 0x1401c7158
            _citizen = nullptr;
        }
    }
    ser.Serialize(_toolId, nullptr);   // +0x28 int
}

// ============================================================
// ShowTool::AbstractImpl / _purecall (0x140042710)
// If toolId > 10 and != 0xFFFFFFFE, hide tool. Otherwise show tool.
// Always returns 1.
// ============================================================

int  ShowTool::AbstractImpl(ActionStack* actionStack) {
    (void)actionStack;
    // B6 (14d G73 / G74): 0x14004271a-0x140042772 tests neither the citizen nor its removal:
    // toolId > 10 (unsigned) and != -2 -> 0x1401176e0 on [+0x20]; else 0x1401173b0 on
    // [+0x20] with the toolId (the carry path, -2, included).
    uint32_t toolId = static_cast<uint32_t>(_toolId);
    if (toolId > 10 && toolId != 0xFFFFFFFE) {
        _citizen.Get()->HideTool();   // 0x1401176e0
        return 1;
    }
    _citizen.Get()->ShowTool(static_cast<int>(toolId));   // 0x1401173b0
    return 1;
}

// ============================================================
// ShowTool::CheckEntity (0x1400426d0)
// Hides tool on the citizen at +0x20: CheckPtr, then a tail jump to 0x1401176e0 with no
// test (B6 14d G73).
// ============================================================
void ShowTool::CheckEntity() {
    _citizen.Get()->HideTool();   // 0x1401176e0
}

// ============================================================
// ShowTool::GetTarget (0x140042680)
// Returns true if the citizen's (+0x20) owning entity (Component +0x10) matches the param:
// 0x14004269a / 0x1400426a0 read [+0x20] then its +0x10 with no test (B6 14d G73).
// ============================================================
bool ShowTool::GetTarget(ComponentEntity** param) {
    return _citizen->_entity.Get() == *param;
}

// ============================================================
// class ShowTool (ShowTool.h) — ENDGAME B.3.8 ACT.
// The hand-built g_ShowTool_Vtable[10], its per-class type-id cache and the
// Action_Alloc / Action_InitInPlace factories are gone: the compiler emits
// the table, DECLARE_EXE_TYPE answers slots 1/2.
// ============================================================
#include "Object/factory_registration.h"

// create 0x1400013b0. Retail's default ctor (inlined, 0x14000140c) stores +0x28 = -1;
// the tree's factory zero-filled every field instead. B38 B8-1: the retail stores are kept; the
// fields retail leaves unwritten stay zero-filled (MemAlloc gives no guarantee).
ShowTool::ShowTool()
    : _citizen(nullptr),
      _toolId(-1),
      _pad2C{} {}

// [3] 0x140042780
// [4] 0x140042710
// [7] 0x140042700 — the leaf's own `RET 0` (same bytes as the base body)
void ShowTool::Exit(ActionStack*) {}
// [8] 0x1400426d0
// [9] 0x140042680

::Object::Base* ShowTool::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ShowTool), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ShowTool());
}
::Object::Base* ShowTool::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ShowTool());
}


// ============================================================
// FUN_140042890 — ShowTool_Action_Ctor
//   RCX=self, RDX=param_2 ({inner ptr}), R8D=toolType (int @ +0x28).
//   Base Internal ctor; Action vtable -> ShowTool vtable; +0x20 = *param_2; +0x28 = toolType.
// ============================================================

// 0x140042890: Action() (Internal, +0x10/+0x18 = 0), then +0x20 = *param_2 (the
// door's double store `+0x20 = 0; +0x20 = *param_2`: the InternalPtr's own ctor stores
// the 0), +0x28.
ShowTool::ShowTool(CitizenComponent* const* citizen, int toolId)
    : _citizen(*citizen), _toolId(toolId) {}

DEFINE_EXE_TYPE(ShowTool, "Action")
