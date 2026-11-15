#pragma once
// src/game/Entity/exe_bodies.h -- declarations of the exe bodies DEFINED in
// src/game/Entity/ (ENDGAME B.3R, block W0-P0c, B3R_RULES R5).
//
// Also here: the declarations and inline bodies of this module that lived in
// engine/funcs/engine_funcs.h, moved verbatim with their comments (that
// header is gone since B3R W2-EZ: its includers include this one directly).
//
// Declared in another header already (not redeclared here):
//   ComponentEntity::PostUpdateComponents  0x1400c2c70  (src/game/Entity/ComponentEntity.h)
//   EntityList::PreUpdate                  0x1400c3bd0  (src/game/Entity/EntityList.h)
//   EntityList::PostUpdateStaging          0x1400c3ab0  (src/game/Entity/EntityList.h)
//   EntityList::RemoveEntity               0x1400c38e0  (src/game/Entity/EntityList.h)

#include <cstdint>
#include "object/TypeInfo.h"   // ::Object::Base (FindComponentByTypeId)
#include "Entity/ComponentEntity.h"   // ComponentEntity (the finders' receiver)
class AttendanceComponent;
class BuildComponent;
class CemeteryComponent;
class CitizenComponent;
class ClearAreaComponent;
class ConsumeProduceComponent;
class DestroyComponent;
class FireComponent;
class ImmigrationComponent;
class MapComponent;
class ModelComponent;
class MultiObjectComponent;
class OrchardComponent;
class PastureComponent;
class StateMachineComponent;
class StatusIconComponent;
class StorageComponent;
class StorageLocationComponent;
class TrackerComponent;
class TradeComponent;
class WorkComponent;
class WorkPlaceComponent;

// ---- entity_findcomponent.cpp
// (B8 ENT: the FindComponent family is ComponentEntity::FindComponent<T>; the file is gone.)
