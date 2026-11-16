// src/game/Work/work_base_accessor.cpp
// Shared Work accessor helpers: the file holds no body (see the note below).
#include "Work/exe_bodies.h"

#include <new>
#include "object/TypeInfo.h"
#include "Action/exe_bodies.h"
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/CropFieldComponent.h"       // ENDGAME B.3.9 D5
#include "Component/OrchardComponent.h"         // ENDGAME B.3.9 D5
#include "Component/RawMaterialDescription.h"   // ENDGAME B.3.9 D5
#include "Component/AutoPickupComponent.h"      // ENDGAME B.3.9 CB: 0x1401c3fb0 cell class
#include "Entity/ComponentEntity.h"   // ComponentEntity::FindComponent<T>
#include "Component/ComponentDescription.h"
#include "Component/Description.h"

// (FindComponent instances 0x140005380 / 0x140005440 / 0x140022520, the B4 FORWARD
// forwarders to ComponentEntity::FindComponent<CropFieldComponent / OrchardComponent /
// AutoPickupComponent>, had no caller left: deleted, B4 P2 WORK-3.)
