// src/game/Work/work_base_standalone.cpp
// The Work base class (ENDGAME B.3.8, Work/Work.h): its ctors, its own slots and its
// compiler table. The factory-cascade helpers that used to live here (Work_Alloc,
// Work_GenericCtor, Work_GenericScalarDtor, Work_Slot1/5/12, Work_SlotRet0,
// Work_Slot11_HasActiveTasks, Work_SerializeNop, Work_PureCall) went with the last
// hand-built leaf table: every leaf is a real class now.

#include "Object/factory_registration.h"   // Object::GetTypeIdByName
#include "Object/exe_type.h"               // DEFINE_EXE_TYPE
#include "Work/Work.h"                     // Work (ENDGAME B.3.8), WorkAddr::Vtable
#include "object/Internal.h"               // Object::detail::InternalMemFree
#include <cstdint>
#include <cstdio>
#include "Entity/ComponentEntity.h"   // B4 S4 RT2: InternalPtr<ComponentEntity>::Get() needs the class complete
#include "Component/WorkComponent.h"   // B4 S4 RT2: InternalPtr<WorkComponent>::Get() needs the class complete

// ============================================================================
// Work — ENDGAME B.3.8 block WRK (Work.h). Retail table 0x140161a80.
// ============================================================================

// The default base init every retail leaf ctor inlines after `??0Internal`
// (e.g. BuildRoad_Ctor 0x1400e4ad0). The 0x140161a80 stamp is the compiler's.
Work::Work()
    : Object::Internal(),
      field_10(nullptr), field_18(nullptr),
      field_20(-1), field_24(0), field_28(-1), field_2c(0),
      field_30{0}, field_38{0}, field_40(0) {}

// 0x1400f72e0 (Work_Ctor25): `??0Internal`, the compiler's stamp, then
// Work::InitFromEntity (work_base_reimpl.cpp).
Work::Work(ComponentEntity* const* pEntityIBP) : Object::Internal() {
    InitFromEntity(pEntityIBP);
}

// [1] TypeName / [2] Type, the record and retail's abstract factory (create =
// the logging stub, in-place = null): ENDGAME B.3.9 RF.
DEFINE_EXE_ABSTRACT_TYPE(Work, "Object::Internal")

// [5] 0x1400f7540 `XOR AL,AL; RET` (shared by 22 tables).
int  Work::Slot5(CitizenComponent**, System::Time*) { return 0; }
// [12] 0x140006b10 `RET 0` (shared by 25 tables).
void Work::Slot12(int /*version*/) {}


