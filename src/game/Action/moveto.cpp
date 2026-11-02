// src/game/Action/moveto.cpp
// Clean C++ reimplementation of MoveTo action methods.
// Source: Game/Action/MoveTo.cpp (from profile build debug strings)
//
// MoveTo -- 0x480 bytes (1152!). Full citizen pathfinding and movement.
// Inherits: Object::Internal -> Action (0x20) -> MoveTo
// Vtable at 0x14015fe40 (10 slots).
//
// Key fields:
//   +0x20: targetId / pathfinder slot index (int, -1 = invalid)
//   +0x24: path version (int, 0 = no path cached)
//   +0x28: target entity (InternalBasePtr)
//   +0x30: path data pointer (External ptr to entity description)
//   +0x38: destination X (float)
//   +0x3c: destination Y (float)
//   +0x40: destination Z (float)
//   +0x48: (unused/padding?)
//   +0x5c: terrain flags for pathfinding (uint)
//   +0x60: current path index (int, -1 = finished)
//   +0x64: path node count (int)
//   +0x68: path node array (int[0x100] -- cell indices)
//   +0x468: idle animation ID (int)
//   +0x46c: walk animation ID (int)
//   +0x470: bridge flag (int)
//   +0x474: road flag (bool/byte)
//   +0x478: speed multiplier (float, default 1.0)
//
// Decompiled from Application-x64.exe using Ghidra.

#include "MoveTo.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/terrain_sidecar.h"
#include "Action/exe_bodies.h"   // ActMv_*, Pf_SpatialProximityQuery_92260
#include "Work/exe_bodies.h"     // WorkImpl_Sqrtf_132010
#include "Game/exe_constants.h"  // kRdata_*
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch C
#include "math/Math.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G
#include "Component/CharacterComponent.h"
#include "Component/CharacterGrid.h"     // CharacterGrid::QueryNearestCharacter
#include "Component/ModelComponent.h"
#include "Component/Description.h"
#include "Game/Map.h"
#include "Game/PathfinderThread.h"
#include "Component/PopulationComponent.h"
#include "Component/CharacterDescription.h"   // CharacterDescription::TypeId() (moved ctor)
#include "system/Value.h"      // System::Random (moved ctor)
#include "Entity/ComponentEntity.h"   // B4 S4 RT2: InternalPtr<ComponentEntity>::Get() needs the class complete
#include "ext/registry.h"                      // EXT SEAM (NOT RETAIL): Ext::Fix().stranded_wade
#include "ext/stranded_wade/stranded_wade.h"   // EXT SEAM (NOT RETAIL, docs/retail_bugs.md §19)

// ============================================================
// Helpers, called by name (B3R)
// ============================================================

namespace {
// The .rdata float constants (Game/exe_constants.h) that are stored as bit patterns.
inline float RdataF(uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; }
// 0x1400032e0 (Array<Ptr8>::SetSize) is the exe-side Collection::Array<T>
// monomorphisation -- ENDGAME B.3.2 step 2 converted its one call site below to a
// member call on the typed array, so its resolved-pointer stand-in
// is gone.
} // namespace

// ENDGAME B.3.1 step 2: Math::Vector3::Serialize was read from the IAT slot at
// 0x140152a68; it is now a direct member call on Math::Vector3.

//   0x140022970 ActMv_ExternalPtrSerialize_22970 -- ExternalPtr serialize (path description)
//   import slot 0x140152a98 ?IsDerivedFrom@Base@Object@@QEBA_NI@Z -> Object::Base::IsDerivedFrom
//               (both readers test the object for null first)

// ENDGAME B.3.1 step 2: the Math value-type helpers below used to be read from the
// exe's IAT dispatch slots, each with the DLL boundary's (this, hidden-retptr, args)
// shape. Runtime-x64.dll is gone, so they are now plain member calls on the real
// Math:: classes (src/engine/math/Math.h), which return by value and let the
// compiler place the result. The slots they came from, for provenance:
//   0x140152458  ?Dot@Vector2@Math@@QEBA?AVScalar@2@AEBV12@@Z   -> Vector2::Dot
//   0x1401526b0  ?Normalize@Vector2@Math@@QEBA?AV12@XZ          -> Vector2::Normalize
//   0x140152a30  ??0Vector3@Math@@QEAA@MMM@Z                    -> Vector3(x,y,z)
//   0x140151268  ??0Vector3@Math@@QEAA@AEBVScalar@1@0 0@Z       -> Vector3(Scalar,Scalar,Scalar)
//   0x1401512c0  ?Normalize@Vector3@Math@@QEBA?AV12@XZ          -> Vector3::Normalize
//   0x1401512d8  ?Heading@Vector3@Math@@QEBAMXZ                 -> Vector3::Heading
//   0x140151298  ?Dot@Vector3@Math@@QEBA?AVScalar@2@AEBV12@@Z   -> Vector3::Dot
//   0x140152a68  ?Serialize@Vector3@Math@@QEAAXAEAVSerializer@IO@@@Z -> Vector3::Serialize
// The historical R8-was-uninitialised bug on Vector2::Dot (a 2-arg call left the
// "other" operand garbage and produced NaN distances) cannot recur: the operand is
// now a real function parameter.

//   0x140132010 WorkImpl_Sqrtf_132010      -- sqrtf implementation
//   0x14010a1f0 ActMv_SetAnimation_a1f0    (AnimalComponent_impl_a1f0) -- set animation on
//               entity (entity_ptr, animId, param3_float, param4_float)
//   0x140092260 Pf_SpatialProximityQuery_92260 -- bridge entity query near position
//               (mapBridges, outResult, position, radius, flags, excludeEntity)
//   0x1400c8c10 ActMv_PathSetup_c8c10      (MoveTo_helper_8c10) -- path setup (world pos to
//               cell, A* query)
//   0x1400c8d20 ActMv_UpdateFacing_c8d20   (MoveTo_sub_8d20) -- update facing direction
//               toward next waypoint
//   0x14009e550 ActMv_SetTerrainCosts_e550 / 0x14009cfb0 ActMv_PathfinderQuery_9cfb0 (not
//               called in this file)
//   0x1400a6d50 ActMv_CopyPathFromFinder_a6d50 (MoveTo_helper_6d50) -- copy path from
//               pathfinder result into path array

// Constants (Game/exe_constants.h)
// DAT_1401720d4 = 0.05f (minimum distance threshold)
// DAT_140172114 = 0.5f (half-cell offset for cell-to-world conversion)
// DAT_14017214c = 1.0f (g_fUVScale, used for animation params)

// ============================================================
// Helper: clamp int to [0, mapSize-1]
// ============================================================
static inline int ClampCell(int val, int mapSize) {
    if (val < 0) return 0;
    if (val >= mapSize) return mapSize - 1;
    return val;
}

// ============================================================
// Helper: world position to clamped cell index
// ============================================================
static inline int WorldToCell(int mapSize, float worldX, float worldY) {
    float halfMap = (float)(mapSize >> 1);
    int cellX = ClampCell((int)(worldX + halfMap), mapSize);
    int cellY = ClampCell((int)(worldY + halfMap), mapSize);
    return mapSize * cellY + cellX;
}

// ============================================================
// MoveTo::Serialize (0x1400c8ec0)
// Versioned serialization. Version 0 = status, Version 1 = all path data.
// Fields: +0x18 status, +0x28 target IBP, +0x30 path desc ExternalPtr,
//   +0x64 pathNodeCount, +0x60 currentPathIndex, +0x68 pathNodes[count],
//   +0x38 destination Vec3, +0x5c terrainFlags, +0x468 idleAnimId,
//   +0x46c walkAnimId, +0x470 bridgeFlag (version>0), +0x474 roadFlag,
//   +0x478 speedMultiplier
// ============================================================
void MoveTo::Persist(IO::Stream& stream) {
    Action::Persist(stream);   // inlined in retail: Serializer(stream, 0), status +0x18

    // Version 1: serialize all movement data
    IO::Serializer ser(stream, 1);

    // The moving character (+0x28)
    character.InternalBasePtr::Serialize(ser);
    if (character.Get() != nullptr) {
        bool valid = character->IsDerivedFrom(CharacterComponent::TypeId());
        if (!valid) {
            character = nullptr;
        }
    }

    // The character's description ExternalPtr (+0x30)
    ::Object::SerializeExternal<CharacterDescription>(ser, description);   // 0x140022970

    // Path node count (+0x64) and current index (+0x60) as ints
    ser.Serialize(pathCount, nullptr);
    ser.Serialize(pathIndex, nullptr);

    // Path node array (+0x68), count from +0x64
    ser.Serialize(pathCount, path, nullptr);

    // Destination position (+0x38) as Vector3
    destination.Serialize(ser);

    ser.Serialize(passFlags, nullptr);       // +0x5C uint
    ser.Serialize(moveAnimation, nullptr);   // +0x468
    ser.Serialize(stopAnimation, nullptr);   // +0x46C

    // Version check for the movement profile
    int version = ser.GetVersion();
    if (version == 0) {
        // Version 0 compatibility: deserialize and discard a legacy IBP field
        ::Object::InternalBasePtr tempIBP;
        tempIBP.Serialize(ser);
        if (tempIBP.Get() != nullptr) {
            ::Object::IsDerivedFromNullSafe(tempIBP.Get(), PopulationComponent::TypeId());
        }
    } else {
        // Version > 0: serialize the movement profile (+0x470)
        version = ser.GetVersion();
        if (version > 0) {
            ser.Serialize(movementProfile, nullptr);
        }
    }

    ser.Serialize(field_0x474, nullptr);       // +0x474 bool
    ser.Serialize(speedMultiplier, nullptr);   // +0x478 float
}

// ============================================================
// MoveTo::AbstractImpl (0x1400c8ab0)
// Checks distance to destination. If close enough (< 0.05), returns 0 (arrived).
// Otherwise sets up A* path and returns 1 (moving) or 2 (no path).
// ============================================================
int  MoveTo::AbstractImpl(ActionStack* actionStack) {
    (void)actionStack;

    // Current character position (CharacterComponent -> ModelComponent translation)
    const Math::Vector3& cur = character->modelComponent->transform.translation;
    float curX = cur.x;
    float curY = cur.y;

    // Destination position from MoveTo fields
    float destX = destination.x;
    float destY = destination.y;

    // Compute 2D distance squared: (curX - destX)^2 + (curY - destY)^2
    float dx = curX - destX;
    float dy = curY - destY;
    Math::Vector2 vec2(dx, dy);
    float dotResult = vec2.Dot(vec2).v;        // this-dot → dx²+dy²
    float dist = WorkImpl_Sqrtf_132010(dotResult);

    // If already close enough, return 0 (arrived)
    if (dist < kRdata_1401720d4) {
        return 0;
    }

    // Get current position again (re-read since CheckPtr may update)
    const Math::Vector3& cur2 = character->modelComponent->transform.translation;

    float worldPos[3];
    worldPos[0] = cur2.x;
    worldPos[1] = cur2.y;
    worldPos[2] = cur2.z;

    // Set up A* path from current position to destination
    PathSetup(worldPos);   // 0x1400c8c10

    // Check path setup result
    int32_t pathVersion = ticket.token;
    int32_t pathSlotId = ticket.slot;
    int32_t pathIndex = this->pathIndex;


    if (pathVersion == 0 || pathSlotId < 0) {
        // No valid path cached
        if (pathIndex < 0) {
            // EXT SEAM (NOT RETAIL, docs/retail_bugs.md §19): fix.stranded_wade counts a refusal
            // on a land-to-land zone mismatch (src/ext/stranded_wade/).
            if (Ext::Fix().stranded_wade) Ext::StrandedWade::OnRefused(this);
            return 2;  // No path possible
        }
    } else {
        // Path cached but current index not started
        if (pathIndex < 0) {
            return 1;  // Wait for path resolution
        }
    }

    // Path is ready -- update facing direction toward first waypoint
    UpdateFacing();   // 0x1400c8d20

    // Start the move animation
    character.Get()->SetAnimation(static_cast<uint32_t>(moveAnimation), kRdata_14017214c, kRdata_14017214c);   // 0x14010a1f0

    return 1;
}

// ============================================================
// MoveTo::Execute (0x1400c8570)
// DISABLED HOOK (bug class 3: MOV RAX,RSP in prologue).
// Full A* path traversal along waypoints:
//   - If no current path index (+0x60 == -1): check for cached path, copy from
//     pathfinder, or call AbstractImpl (vtable +0x20) for re-query.
//   - If traversing: compute target cell from path array, check terrain type
//     for speed (road/bridge/normal), update character speed, advance index.
//   - If path exhausted: reset to idle animation.
// Returns: 0=arrived, 1=moving, 2=no path
// ============================================================

// PERF (2026-09-04): these were bit-identical inline copies of the trivial Runtime
// math exports the hot paths called through dispatch slots (a DLL call per 3-float
// store). ENDGAME B.3.1 step 2 (2026-09-13) retires the Math ones: Runtime-x64.dll is
// gone, and the classes in src/engine/math/Math.h now carry those same bodies —
// Vector3::Dot still evaluates (y, x, z) in that order, so the float result is
// unchanged to the bit.

int  MoveTo::Execute(ActionStack* actionStack, const System::Time* time) {
    (void)time;

    Map* map = GameUpdateLoop::_singleton->m_game;

    // Get character's current world position
    const Math::Vector3& cur = character->modelComponent->transform.translation;
    float curX = cur.x;
    float curY = cur.y;
    float curZ = cur.z;

    int32_t pathIndex = this->pathIndex;

    // ---- CASE 1: No active path segment (pathIndex == -1) ----
    if (pathIndex == -1) {
        int32_t pathVersion = ticket.token;

        if (pathVersion != 0) {
            // Have a cached path version -- try to retrieve from pathfinder
            int32_t slotId = ticket.slot;
            if (slotId >= 0) {
                PathfinderThread* pathfinder = map->pathfinder;

                // Check if slot is still valid
                if (slotId >= pathfinder->results.size) {
                    return 1;  // Slot out of range, wait
                }

                // Verify path version matches
                const PathResult& result = pathfinder->results.data[slotId];
                if (result.token != pathVersion) {
                    return 1;  // Version mismatch, wait for re-query
                }

                // Check if path is ready (slot active flag)
                if (result.ready == 0) {
                    return 1;  // Path not yet computed
                }

                // Copy path nodes from pathfinder into our local array
                int nodeCount = pathfinder->TakePath(&ticket, path, 0x100);   // 0x1400a6d50
                pathCount = nodeCount;

                if (nodeCount == 0) {
                    // EXT SEAM (NOT RETAIL, docs/retail_bugs.md §19): fix.stranded_wade, a failure
                    // that is not a zone refusal.
                    if (Ext::Fix().stranded_wade) Ext::StrandedWade::OnEmptyPath(this);
                    return 2;  // Path found but empty -- no valid route
                }

                // Start traversal at index 1 (skip start cell)
                this->pathIndex = 1;

                // Update facing direction
                UpdateFacing();   // 0x1400c8d20

                // Start the move animation
                character.Get()->SetAnimation(static_cast<uint32_t>(moveAnimation), kRdata_14017214c, kRdata_14017214c);   // 0x14010a1f0
                return 1;
            }
        }

        // No cached path or invalid slot -- call AbstractImpl (vtable slot +0x20,
        // a virtual dispatch in retail)
        int result = AbstractImpl(actionStack);
        if (result != 2) {
            return 1;
        }
        return 2;
    }

    // [COPY-PINNED begin] tests/unit/copy_drift_test.cpp pins the text from here to
    // the end marker: tests/unit/ext_walk_batch_test.cpp (ExecuteRef) holds a literal
    // copy of this case over plain structs. A change here fails that test until the copy
    // is re-synced and the hash re-pinned.
    // ---- CASE 2: Active path traversal (pathIndex >= 0) ----
    int mapSize = map->size;

    // Compute destination cell from +0x38/+0x3c
    float destX = destination.x;
    float destY = destination.y;
    int destCell = WorldToCell(mapSize, destX, destY);

    // Compute current position cell
    int curCell = WorldToCell(mapSize, curX, curY);

    // Check if we've reached the destination cell
    if (curCell == destCell) {
        // At destination -- compute precise distance
        float dz = curZ - destination.z;
        float dy2 = curY - destY;
        float dx2 = curX - destX;
        Math::Vector3 diffVec(dx2, dy2, dz);

        Math::Vector2 dotVec(diffVec.x, diffVec.y);
        float dotOut = dotVec.Dot(dotVec).v;   // this-dot → dx²+dy²
        float distToGoal = WorkImpl_Sqrtf_132010(dotOut);

        if (distToGoal < kRdata_1401720d4) {
            // Close enough -- arrived
            return 0;
        }

        // Not close enough yet -- compute direction and set facing
        float revDx = destX - curX;
        float revDy = destY - curY;
        float revDz = destination.z - curZ;
        Math::Vector3 revVec(revDx, revDy, revDz);

        // Normalize the 2D direction.
        Math::Vector2 dir2d(revVec.x, revVec.y);
        Math::Vector2 dir2dN = dir2d.Normalize();

        // Build 3D facing vector and set on character
        Math::Vector3 facing3d(dir2dN.x, dir2dN.y, 0.0f);
        CharacterComponent* mover = character.Get();
        mover->field_0x28 = false;  // Clear animation lock flag

        // Build normalized facing vector from 2D components
        float scalarX = facing3d.x;
        float scalarY = facing3d.y;
        float scalarZ = 0.0f;
        Math::Vector3 tempVec(scalarX, scalarY, scalarZ);   // Vector3(Scalar,Scalar,Scalar) = 3 stores
        Math::Vector3 normalized = tempVec.Normalize();

        // Write facing direction to character (+0x2c, +0x30, +0x34)
        mover->field_0x2C.x = normalized.x;
        mover->field_0x2C.y = normalized.y;
        mover->field_0x2C.z = normalized.z;

        // Compute heading angle from facing direction
        float heading = facing3d.Heading();
        mover->field_0x3C = heading;

        return 1;
    }

    // Not at destination cell yet -- check if traversing path
    int32_t nodeCount = pathCount;
    if (nodeCount < 2) {
        // Path too short, just keep moving
        return 1;
    }

    // Check if we've reached the end of the path nodes
    if (pathIndex >= nodeCount) {
        // Path exhausted -- stop animation and reset path
        character.Get()->SetAnimation(static_cast<uint32_t>(stopAnimation), kRdata_14017214c, kRdata_14017214c);   // 0x14010a1f0
        this->pathIndex = -1;
        return 1;
    }

    // Look up terrain type at the citizen's CURRENT cell for the speed calculation.
    // Verified vs the real MoveTo::Execute (0x1400c8570): the terrain byte is read at
    // iVar11 = curCell (the cell the citizen currently occupies), NOT destCell. Using
    // destCell here samples the wrong tile's movement cost.
    // [terrain sidecar] same byte, read through the 1-byte-per-cell mirror
    // (Game/terrain_sidecar.h) instead of the 0x30-stride cell record.
    uint8_t terrainType = TerrainSidecar::Get(map->cells.GetData(),
                                            map->cells.GetSize(), curCell);

    float speed;
    bool onRoad = field_0x474;

    if (onRoad) {
        // Following: the description's +0x30 speed
        speed = description->_f30;
    } else {
        // Check terrain type for road tiles
        // terrainType & ~0x08 in {1, 0} means road/path tile
        if (((terrainType - 1) & 0xFFFFFFF7) == 0) {
            // Road tile -- use road speed
            speed = description->_f34;
        } else if (terrainType == 0) {
            // Normal terrain -- use normal speed
            speed = description->_f38;
        } else {
            // Default speed (e.g., grass) -- use g_fUVScale (1.0)
            speed = kRdata_14017214c;
        }
    }

    // Apply speed multiplier and set on character
    character->field_0x38 = speed * speedMultiplier;

    // Check if the citizen's CURRENT cell has reached the current waypoint cell.
    // Verified vs the real MoveTo::Execute (0x1400c8570): the comparison is
    // iVar11 (curCell) != pathNodes[pathIndex], NOT destCell. The prior port compared
    // the FINAL destination cell against the waypoint, so the index only advanced when
    // pathIndex already pointed at the last node — intermediate waypoints never fired,
    // UpdateFacing was never re-called, and the citizen bee-lined in a fixed heading
    // past its target forever instead of steering along the A* path.
    int32_t waypointCell = path[pathIndex];
    if (curCell != waypointCell) {
        // Haven't reached the current waypoint yet, keep moving
        return 1;
    }

    // Reached the waypoint -- advance to next
    this->pathIndex = pathIndex + 1;

    // Update facing direction for next segment
    UpdateFacing();   // 0x1400c8d20

    return 1;
    // [COPY-PINNED end]
}

// ============================================================
// MoveTo::Enter (0x1400c8970)
// Queries the CharacterGrid for the nearest other character within 0.5; if one is found
// at a higher address facing our way (dot > 0), sets +0x474.
// ============================================================
void MoveTo::Enter(ActionStack*, const System::Time*) {
    // Read current position
    const Math::Vector3& cur = character->modelComponent->transform.translation;
    float position[3];
    position[0] = cur.x;
    position[1] = cur.y;
    position[2] = cur.z;

    // Query for the nearest character
    Math::Scalar radius(kRdata_140172114);   // 0.5f search radius
    CharacterComponent* bridgeResult = nullptr;
    CharacterGrid* bridgeMgr = GameUpdateLoop::_singleton->m_characterGrid;
    CharacterComponent* bridgeExclude = character.Get();

    bridgeMgr->QueryNearestCharacter(&bridgeResult,   // 0x140092260
                          position, &radius, 0, &bridgeExclude);

    if (bridgeResult != nullptr) {
        // Found a nearby character -- only one at a higher address than ours
        if (character.Get() < bridgeResult) {
            // Compare facing directions via dot product
            CharacterComponent* mover = character.Get();

            // Our character's facing direction (+0x2c..+0x34)
            Math::Vector3 myFacing(mover->field_0x2C.x,
                                   mover->field_0x2C.y,
                                   mover->field_0x2C.z);

            // The other character's facing direction
            Math::Vector3 bridgeFacing(bridgeResult->field_0x2C.x,
                                       bridgeResult->field_0x2C.y,
                                       bridgeResult->field_0x2C.z);

            // this = bridgeFacing, other = myFacing (the original's RCX / R8).
            float dotOut = bridgeFacing.Dot(myFacing).v;
            if (dotOut > 0.0f) {
                field_0x474 = true;
                return;
            }
        }
    }

    // None, or facing away -- clear the flag
    field_0x474 = false;
}

// ============================================================
// MoveTo::Exit (0x1400c8520)
// Plays the stop animation (+0x46c).
// ============================================================
void MoveTo::Exit(ActionStack* actionStack) {
    (void)actionStack;
    // EXT SEAM (NOT RETAIL, docs/retail_bugs.md §19): fix.stranded_wade, an arrival resets the count.
    if (Ext::Fix().stranded_wade) Ext::StrandedWade::OnExit(this);
    character.Get()->SetAnimation(static_cast<uint32_t>(stopAnimation), kRdata_14017214c, kRdata_14017214c);   // 0x14010a1f0
}

// ============================================================
// MoveTo::CheckEntity (0x1400c84d0)
// Same body as Exit -- plays the stop animation.
// ============================================================
void MoveTo::CheckEntity() {
    character.Get()->SetAnimation(static_cast<uint32_t>(stopAnimation), kRdata_14017214c, kRdata_14017214c);   // 0x14010a1f0
}

// ============================================================
// MoveTo::GetTarget (0x1400c8480)
// Returns true if the given param (InternalBasePtr) matches the character's owning
// entity (Component +0x10).
// ============================================================
bool MoveTo::GetTarget(ComponentEntity** param) {
    return character->_entity.Get() == *param;
}

// ============================================================
// class MoveTo (MoveTo.h) — ENDGAME B.3.8 ACT.
// The hand-built g_MoveTo_Vtable[10], its per-class type-id cache and the
// Action_Alloc / Action_InitInPlace factories are gone: the compiler emits
// the table, DECLARE_EXE_TYPE answers slots 1/2.
// ============================================================
#include "Object/factory_registration.h"
#include "object/class_vptr.h"   // Object::ClassVptr (the ctor records the vptr hint)

// create 0x1400228c0. Retail's default ctor (0x1400c90e0) stores +0x20 = -1, +0x60 = -1, +0x478 = 1.0f;
// the tree's factory zero-filled every field instead. B38 B8-1: the retail stores are kept; the
// fields retail leaves unwritten stay zero-filled (MemAlloc gives no guarantee).
MoveTo::MoveTo()
    : ticket{-1, 0},
      character(nullptr),
      description(nullptr),
      destination(),
      field_0x44(0),
      field_0x48(0),
      field_0x4C(0),
      field_0x50(0),
      field_0x54(0),
      field_0x58(0),
      passFlags(0),
      pathIndex(-1),
      pathCount(0),
      path{},
      moveAnimation(0),
      stopAnimation(0),
      movementProfile(0),
      field_0x474(false),
      _pad475{},
      speedMultiplier(1.0f),
      _pad47C{} {
    Object::ClassVptr<MoveTo>::Record(this);   // NOT RETAIL: the vptr hint (object/class_vptr.h)
}

// [3] 0x1400c8ec0
// [4] 0x1400c8ab0
// [5] 0x1400c8570
// [6] 0x1400c8970
// [7] 0x1400c8520
// [8] 0x1400c84d0
// [9] 0x1400c8480

::Object::Base* MoveTo::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(MoveTo), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) MoveTo());
}
::Object::Base* MoveTo::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) MoveTo());
}


// ============================================================
// FUN_1400c9170 — MoveTo_Action Ctor
// Base Internal ctor, Action -> MoveTo vtable; inits fields, copies destination vec3
// (+0x38..+0x40), the character IBP (+0x28 from param_2), keeps the character's
// description at +0x30 when it IsDerivedFrom CharacterDescription (DAT_1401c70b0). Speed:
// if >=0 use it, else a randomised default = RandFloatTol(world.random) * DAT_1401720e4 +
// DAT_14017214c.
// Note: hidden float arg `speed` arrives in XMM (XMM2 per the prologue), param order
// matches Ghidra (pathType, param_4, destination, param_6, param_7, speed).
// ============================================================

// 0x1400c9170 — the ctor body: after Action() the retail stores, in retail order.
MoveTo::MoveTo(CharacterComponent* const* characterRef, uint32_t moveAnim, int stopAnim,
               const float* dest, uint32_t passFlagsArg, int profile, float speed) {
    Object::ClassVptr<MoveTo>::Record(this);   // NOT RETAIL: the vptr hint (object/class_vptr.h)
    ticket.slot = -1;
    ticket.token = 0;
    character = *characterRef;   // +0x28 (the InternalPtr's own ctor stored 0 first, as retail)
    description = nullptr;
    destination.x = dest[0];
    destination.y = dest[1];
    destination.z = dest[2];
    // H7: retail zeroes +0x44..+0x5B with three qword stores.
    field_0x44 = 0;
    field_0x48 = 0;
    field_0x4C = 0;
    field_0x50 = 0;
    field_0x54 = 0;
    field_0x58 = 0;
    passFlags = passFlagsArg;
    pathIndex = -1;
    pathCount = 0;
    moveAnimation = static_cast<int32_t>(moveAnim);
    stopAnimation = stopAnim;
    movementProfile = profile;
    field_0x474 = false;
    Description* obj = character->_description;
    // DAT_1401c70b0 is CharacterDescription's type-id cell (Table 1).
    // B6 (14d G67): IsDerivedFrom(+0x18, cell 0x1401c70b0) with no zero-id test, and
    // RandFloatTol on [GameUpdateLoop+0xd8] with no test (0x1400c9170's tail).
    bool derived = obj->IsDerivedFrom(CharacterDescription::TypeId());
    description = derived ? static_cast<CharacterDescription*>(obj) : nullptr;
    if (0.0f <= speed) {
        speedMultiplier = speed;
    } else {
        System::Random* random = GameUpdateLoop::_singleton->m_random;
        float r = random->RandFloatTol();
        speedMultiplier = r * RdataF(kRdata_1401720e4) + kRdata_14017214c;   // 0x1401720e4 / 0x14017214c
    }
}

DEFINE_EXE_TYPE(MoveTo, "Action")

// [EXT ext.walk_batch] both ctors record the vptr (Object::ClassVptr<MoveTo>): Ext::WalkBatch::Tick
// (src/ext/walk_batch/walk_batch.cpp) tells a MoveTo top node by it (MoveTo is final), as the walk
// prefetch tells a CharacterComponent. Nothing in the faithful path reads it.
