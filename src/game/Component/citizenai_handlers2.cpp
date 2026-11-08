// src/game/Component/citizenai_handlers2.cpp
//
// Brick 3b handler wave (part 2) — faithful 1:1 C++ ports of three CitizenAIComponent
// STATE HANDLERS that ValidateAndEnterState (citizenai_candidatelist.cpp) dispatches.
// Each makes its state ENTERABLE by building + pushing an Action/Work sequence onto the
// citizen action stack (CharacterComponent+0x48), exactly like the already-ported Wander
// handler (citizenai_handlers.cpp). Transcribed from the Ghidra decompile and disasm-
// verified offset-by-offset.
//
// Handlers ported here (all closure-ready — every exe sub-callee is REGISTERED):
//   0x14010c360  CitizenAI_State_Education      (case 3) -> PerformWork_Ctor10 + dd30 + 7fb0
//   0x14010c580  CitizenAI_State_SeekHappiness  (case 8) -> PerformWork_Ctor5  + dd30 + 7fb0
//                  + (no-target fallback) the MeshInstance "go invisible" render branch
//   0x14010d560  CitizenAI_State_FindWork       (case 10)-> WalkTowards + 122be0/122b00 + 115a50
//
// HANDLERS NOT PORTED IN *THIS* FILE — they live in citizenai_handlers3.cpp.
// (This list used to say "blocked on UNregistered deep helpers"; that stopped being
// true when reg_citizenai_findhelpers.cpp landed db20/e370/ec70/2ac30, and the stale
// wording survived long enough to be filed as PLAN.md §1.2 item 2 "unported". They
// are ported, registered and observed entering in-game — see
// docs/history/waves/exe_closeout/lane_c.md.)
//   0x14010d790  State_Work           -> citizenai_handlers3.cpp (helper db20 @ 0x14010db20)
//   0x14010cd80  State_AdultTransition-> citizenai_handlers3.cpp (e370 @ 0x14008e370 + 2ac30)
//   0x14010d2c0  State_ElderTransition-> citizenai_handlers3.cpp (ec70 @ 0x14008ec70 + 2ac30)
//
// B.4 W1 COMP-b: the handlers are CitizenAIComponent members; `citizen` is the sibling
//   CitizenComponent (+0x38), its fields read by name. The action builders take
//   (CitizenComponent-IBP, ...) and push internally.
//
// Registered exe sub-callees (resolved via ResolveAddr — bodies NOT duplicated here):
//   0x14008dd30 CitizenAI_FindBestTarget_dd30   (citizenai_findtarget.cpp)
//   0x140087fb0 Pf_EntityAnchorCell_7fb0        (reg_pf_road.cpp)
//   0x1400bccb0 PerformWork_Action_Ctor10       (citizenai_actionctors.cpp)
//   0x1400bd070 PerformWork_Action_Ctor5        (citizenai_actionctors.cpp)
//   0x1400bb490 Pathfinding_WalkTowards         (citizenai_actionctors.cpp)
//   0x140002f90 Entity_FindComponent_MeshInstance (WorkBase_FindComponent_2f90)
//   0x140002ed0 Entity_FindComponent_Position   (WorkBase_FindComponent_2ed0)
//   0x140122be0 FUN_140122be0 walkable-pos       (WorkImpl_WalkablePos_122be0)
//   0x140122b00 FUN_140122b00 walkable-pos recur (WorkImpl_WalkablePosRecursive_122b00)
//   0x140115a50 FUN_140115a50                    (AttendanceComponent_ReleasePartnerSlot)
//   0x1400c3470 FUN_1400c3470 notify-entity      (ActHunt_NotifyEntityChanged_c3470)
//
// Engine helpers (engine_funcs.h): fn_InternalBasePtr_CheckPtr (== _slot in decompiles),
//   fn_Time_Ctor_Float, fn_IsDerivedFrom, fn_Graphics_Scene_AddObject, fn_SceneRemoveObject,
//   g_pGameWorldSingleton, g_pGameSettingsPtr.
//
// SELF-CONTAINED-BRICK type-ids: DAT_1401c7938 ("InteractDescription") and
//   DWORD_1401c7190 ("CitizenDescription") are UNFILLED (0xFFFFFFFF) in standalone, so the
//   original's IsDerivedFrom scans would miss and then dereference NULL (+0x28 / +0x120).
//   Resolved self-contained via Object::GetTypeIdByName, exactly like citizenai_findtarget.cpp.
//   TypeNames recovered from each global's *_Register [DATA] writer (Ghidra-verified):
//     DAT_1401c7938 -> InteractDescription_Register  -> "InteractDescription"
//     DWORD_1401c7190 -> CitizenDescription_Register -> "CitizenDescription"
//
// .rdata float const: DAT_1401721fc = 75.0 (the per-state cooldown-stamp seconds; written to
//   the CitizenAIComponent state-timestamp field at the top of Education/SeekHappiness).
//


// graphics/*.h FIRST: they pull src/engine/volume/Volume.h, whose global-scope
// `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()` once <windows.h> has
// been seen through resolve.h.  ENDGAME B.3.5 caller batch I2.
#include "graphics/Scene.h"
// EXT SEAM (NOT RETAIL, docs/extensions/handins/ITEMPRIO.md): ext.item_priority (class C), the finder tiers
#include "ext/item_priority/item_priority.h"
#include "Component/InteractDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/CitizenDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/CitizenAIComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/AttendanceComponent.h"
#include "Component/InteractComponent.h"
#include "Component/StatusIconComponent.h"
#include "Component/ModelComponent.h"   // the model translation
#include "Game/Map.h"                   // Map::size (m_game)
#include "Game/GameSettings.h"          // GameSettings::statusIconEnable
#include "Entity/ComponentEntity.h"
#include "graphics/Mesh.h"
#include "Component/Component.h"   // B.3.x W0: Component slots by name
#include "system/Value.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Game/exe_bodies.h"   // B3R STORAGE: g_gameSettings
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Object/factory_registration.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "scene_watch.h"
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "Game/exe_constants.h"

namespace {

// --- registered exe sub-callees (resolved lazily via ResolveAddr) ---
// dd30: (param_1 listpair, &outBest, goalCellA, goalCellB, &outFoundComp, outVec3, category)
// 7fb0: (InternalBasePtr* ibp, float* outVec3orNull) -> cell idx
// Ctor10: (CitizenComp-IBP, &srcEntityIBP, float* dest, entity-list object)
// Ctor5: (CitizenComp-IBP, &srcEntityIBP, float* dest, entity-list object)
// WalkTowards: (CitizenComp-IBP, u32 completeArg, float* dest, u32 msgA4, float time, u32 flag)
// FindComponent variants: (entity, outIBP) -> outIBP
// 122be0: (entity, transformSub, &posVec3, outVec3, &bestCost) -> bool

// 122b00: (child, 0, &posVec3, outVec3, &bestCost) -> byte
// 115a50: (CitizenComponent, char) -> bool
// c3470: (MeshInstance) -> void


// --- self-resolved type-ids (Brick pattern) ---
static uint32_t s_tid_InteractDescription = 0;   // DAT_1401c7938
static uint32_t s_tid_CitizenDescription  = 0;   // DWORD_1401c7190


static bool s_init = false;
static void EnsureInit() {
    if (s_init) return;
    if (s_tid_InteractDescription == 0)
        s_tid_InteractDescription = InteractDescription::TypeId();
    if (s_tid_CitizenDescription == 0)
        s_tid_CitizenDescription = CitizenDescription::TypeId();
    s_init = true;
}


} // namespace

// ============================================================
// 0x14010c360 — CitizenAIComponent::State_Education  (state id 3)
// ------------------------------------------------------------
// Stamp cooldown (Time(75.0) -> timers[3]). Early-return if the citizen's
// clothingLevel != 0. Otherwise find the best target via dd30 (goal cell = citizen
// ModelComponent position, B-goal = the home anchor cell or -1). If none found:
// clear clothingType, set happinessFlags bit 2, return 0. If found: build the Ctor10
// work sequence toward it and set state flags; return 1.
// ============================================================
uint64_t CitizenAIComponent::State_Education() {
    EnsureInit();

    // cooldown stamp: Time(DAT_1721fc) -> timers[3]
    System::Time timeStamp{};
    RtSystem_Time_Ctor(&timeStamp, kRdata_1401721fc);
    timers[static_cast<int32_t>(CitizenAIState::GetClothing)] = timeStamp;

    if (citizen->clothingLevel != 0) {
        return 0;  // low byte 0 (retail's RAX carries the citizen pointer above it)
    }

    ComponentDescription* local_res8 = nullptr;   // dd30 out: found component (also Ctor10 param_4)
    float dest[3] = { 0.0f, 0.0f, 0.0f };

    // citizen ModelComponent world pos -> grid cell
    const ModelComponent* model = citizen->character->modelComponent.Get();
    int mapSize = GameUpdateLoop::_singleton->m_game->size;
    int half = mapSize >> 1;
    int cx = static_cast<int>(model->transform.translation.x + static_cast<float>(half));
    int cy = static_cast<int>(model->transform.translation.y + static_cast<float>(half));
    if (cx < 0) cx = 0;
    if (mapSize <= cx) cx = mapSize - 1;
    if (cy < 0) cy = 0;
    if (mapSize <= cy) cy = mapSize - 1;

    // home anchor cell (B-goal) or -1
    uint32_t bGoal;
    if (citizen->home == nullptr) {
        bGoal = 0xffffffff;
    } else {
        ComponentEntity* wpIBP = citizen->home.Get();
        bGoal = EntityAnchorCell(&wpIBP, nullptr);
    }

    ComponentEntity* outBest = nullptr;
    CitizenBehavior* listpair = GameUpdateLoop::_singleton->m_citizenList;
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/ITEMPRIO.md):
    // with item priority active for Clothing (category 4) the finder runs once per rank, best first, so a
    // better-ranked garment in a farther storage wins over a worse one in the nearest.
    Ext::ItemPriority::FindTiered(Ext::ItemPriority::kClothing, [&]() -> bool {
        outBest = nullptr;
        local_res8 = nullptr;
        listpair->FindBestTarget(&outBest, static_cast<uint32_t>(mapSize * cy + cx), bGoal,
                      &local_res8, dest, 4);
        return outBest != nullptr;
    });

    if (outBest == nullptr) {
        citizen->clothingType = 0;
        citizen->happinessFlags |= 4;
        return 0;  // low byte 0
    }

    // found: Ctor10(citizen, &foundEntity, dest, foundComponent)
    ComponentEntity* foundEntity = outBest;
    CitizenComponent* ibpHolder = citizen.Get();   // the CitizenComponent in a stack IBP holder
    CitizenAI_PerformWorkCtor10_bccb0(&ibpHolder, &foundEntity, dest, local_res8);

    citizen->statusFlags |= Bits(CitizenStatus::TaskDone);
    citizen->lastErrand = CitizenErrand::None;
    citizen->lastWorkId = -1;

    return 1;  // CONCAT71(.., 1) -> low byte 1
}

// ============================================================
// 0x14010c580 — CitizenAIComponent::State_SeekHappiness  (state id 8)
// ------------------------------------------------------------
// Stamp cooldown (Time(75.0) -> timers[8]). Early-return if toolUses > 0. Find the
// best target via dd30 (category 2). If found: build the Ctor5 work sequence and set
// state flags (return 1). If NOT found: clear toolGrade, set happinessFlags bit 2,
// and run the StatusIconComponent "show icon" branch (faithful inline copy of
// FUN_14012c3f0's value=1/flagIndex=4 path), then return 0.
// ============================================================
uint64_t CitizenAIComponent::State_SeekHappiness() {
    EnsureInit();

    System::Time timeStamp{};
    RtSystem_Time_Ctor(&timeStamp, kRdata_1401721fc);
    timers[static_cast<int32_t>(CitizenAIState::GetTool)] = timeStamp;

    if (0 < citizen->toolUses) {
        return 0;  // low byte 0
    }

    ComponentDescription* local_res8 = nullptr;   // dd30 out: found component (Ctor5 param_4)
    float dest[3] = { 0.0f, 0.0f, 0.0f };

    const ModelComponent* model = citizen->character->modelComponent.Get();
    int mapSize = GameUpdateLoop::_singleton->m_game->size;
    int half = mapSize >> 1;
    int cx = static_cast<int>(model->transform.translation.x + static_cast<float>(half));
    int cy = static_cast<int>(model->transform.translation.y + static_cast<float>(half));
    if (cx < 0) cx = 0;
    if (mapSize <= cx) cx = mapSize - 1;
    if (cy < 0) cy = 0;
    if (mapSize <= cy) cy = mapSize - 1;

    uint32_t bGoal;
    if (citizen->home == nullptr) {
        bGoal = 0xffffffff;
    } else {
        ComponentEntity* wpIBP = citizen->home.Get();
        bGoal = EntityAnchorCell(&wpIBP, nullptr);
    }

    ComponentEntity* outBest = nullptr;
    CitizenBehavior* listpair = GameUpdateLoop::_singleton->m_citizenList;
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/ITEMPRIO.md):
    // with item priority active for Tool (category 2) the finder runs once per rank, best first, so a
    // better-ranked tool in a farther storage wins over a worse one in the nearest.
    Ext::ItemPriority::FindTiered(Ext::ItemPriority::kTool, [&]() -> bool {
        outBest = nullptr;
        local_res8 = nullptr;
        listpair->FindBestTarget(&outBest, static_cast<uint32_t>(mapSize * cy + cx), bGoal,
                      &local_res8, dest, 2);
        return outBest != nullptr;
    });

    if (outBest == nullptr) {
        // --- no target: clear state + run the StatusIconComponent "show" branch ---
        citizen->toolGrade = 0;
        citizen->happinessFlags |= 4;

        StatusIconComponent* mi = citizen->_entity->FindComponent<StatusIconComponent>();
        if (mi) {
            ComponentEntity* inner = mi->_entity.Get();
            // gated by ((inner->_entityFlags >> 10) & 1) == 0  (disasm: SHR 0xa; TEST CL,1).
            // inner is just-CheckPtr'd by the original (never 0 with valid data); guard
            // it to stay crash-safe (skip rather than fault, matching nothing-to-do).
            if (inner != nullptr && (inner->_entityFlags & Bits(ComponentFlags::MarkedForRemoval)) == 0) {
                // OR [mi+0x28 + 0xc8], 0x10   == set status-mask bit 4
                mi->_geometry.activeMask |= 0x10;
                uint32_t changed;
                if (mi->_geometry.activeMask == 0) {
                    // mask empty -> remove from scene
                    if (-1 < mi->iconId) {
                        GameUpdateLoop::_singleton->m_graphicsScene
                            ->RemoveObject(static_cast<uint32_t>(mi->iconId));
                        mi->iconId = -1;
                    }
                    uint32_t before = mi->_flags;
                    uint32_t after = before & 0xfffffffb;
                    mi->_flags = after;
                    changed = after ^ before;
                } else {
                    // mask non-empty -> ensure in scene + run the slot-8 Tick
                    GameSettings* settings = g_gameSettings;
                    if (settings && settings->statusIconEnable != '\0' &&
                        mi->iconId < 0) {
                        uint32_t id = GameUpdateLoop::_singleton->m_graphicsScene
                            ->AddObject(&mi->_geometry);
                        mi->iconId = static_cast<int32_t>(id);
                        SceneWatch_Note(id, &mi->_geometry, "CitizenAI::handlers2 carry-mesh");
                    }
                    System::Time t0{};
                    RtSystem_Time_Ctor(&t0, 0.0f);
                    mi->Tick(&t0);
                    uint32_t before = mi->_flags;
                    uint32_t after = before | 4;
                    mi->_flags = after;
                    changed = after ^ before;
                }
                if ((changed & 0xffff) != 0 && mi->_entity != nullptr) {
                    mi->_entity.Get()->RefreshEntityFlags();   // 0x1400c3470
                }
            }
        }
        return 0;  // low byte 0
    }

    // found: Ctor5(citizen, &foundEntity, dest, foundComponent)
    ComponentEntity* foundEntity = outBest;
    CitizenComponent* ibpHolder = citizen.Get();
    CitizenAI_PerformWorkCtor5_bd070(&ibpHolder, &foundEntity, dest, local_res8);

    citizen->statusFlags |= Bits(CitizenStatus::TaskDone);
    citizen->lastErrand = CitizenErrand::None;
    citizen->lastWorkId = -1;

    return 1;
}

// ============================================================
// 0x14010d560 — CitizenAIComponent::State_FindWork  (state id 10)
// ------------------------------------------------------------
// (at-school path) Resolve the school's entity, find its InteractComponent, read the
// citizen ModelComponent world pos, run the walkable-position search (122be0 over the
// InteractDescription transform sub + 122b00 over each use location). If a walkable
// target is found: push the WalkTowards action toward it (time read from the
// citizen's CitizenDescription educationWorkLength) and set state flags (return 1). Else:
// FUN_140115a50(citizen, 1) (release/reroll the partner slot) and return 0.
// ============================================================
uint64_t CitizenAIComponent::State_FindWork() {
    EnsureInit();

    AttendanceComponent* anchor = citizen->school.Get();
    InteractComponent* posComp = anchor->_entity->FindComponent<InteractComponent>();

    float outVec[3] = { 0.0f, 0.0f, 0.0f };   // local_58 (target out)

    // citizen ModelComponent world pos -> local_48 vec3 (x,y,z packed)
    const ModelComponent* model = citizen->character->modelComponent.Get();
    float pos[3];
    pos[0] = model->transform.translation.x;
    pos[1] = model->transform.translation.y;
    pos[2] = model->transform.translation.z;

    int bestCost = 0x7fffffff;            // local_res8 low 32

    // transform sub-object on the InteractComponent's description (InteractDescription).
    Description* sub = posComp->_description;
    InteractDescription* transform = (sub && Object::IsDerivedFromNullSafe(sub, s_tid_InteractDescription))
                      ? static_cast<InteractDescription*>(sub) : nullptr;
    uint8_t found = 0;
    // +0x28 is InteractDescription::_useNodes (0x28 when the description is not one)
    found = posComp->FindWalkablePos(&transform->_useNodes, pos, outVec, &bestCost);

    // walk each use location (InteractComponent::useLocations (data, _size))
    int childCount = posComp->useLocations._size;
    if (0 < childCount) {
        int i = 0;
        do {
            InteractComponent* child = posComp->useLocations[i].Get();
            uint8_t r = child->FindWalkablePosRecursive(0, pos, outVec, &bestCost);
            found |= r;
            i++;
        } while (i < posComp->useLocations._size);
    }

    if (found != 0) {
        // WalkTowards toward outVec; time = citizen CitizenDescription educationWorkLength
        Description* descRaw = citizen->_description;
        const CitizenDescription* desc = (descRaw && Object::IsDerivedFromNullSafe(descRaw, s_tid_CitizenDescription))
                     ? static_cast<const CitizenDescription*>(descRaw) : nullptr;
        CitizenComponent* ibpHolder = citizen.Get();
        // [STANDALONE GUARD] the desc null-check is ours; the original derefs +0x120
        // unconditionally (would fault if the description type-check failed).
        float walkTime = desc ? desc->educationWorkLength : 0.0f;
        CitizenAI_Pathfinding_WalkTowards_bb490(&ibpHolder, 0xffffffff, outVec, 4, walkTime, 1);

        citizen->statusFlags |= Bits(CitizenStatus::TaskDone);
        citizen->lastWorkId = -1;
        citizen->lastErrand = CitizenErrand::None;

        return 1;
    }

    // 0x140115a50 returns nothing; the only caller (ValidateAndEnterState)
    // reads the low byte, which is 0.
    citizen.Get()->LeaveSchool('\x01');
    return 0;  // low byte 0
}

