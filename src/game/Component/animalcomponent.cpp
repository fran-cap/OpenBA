// src/game/Component/animalcomponent.cpp
// Genuine C++ reimplementation of the AnimalComponent per-animal simulation —
// the deer/livestock wander + aging + death pathway.
//
// AnimalComponent is the per-creature component. Its retail vtable (0x140162110)
// overrides six slots; the five this file implements are:
//
//   [4]  Startup      0x140106d50  seed the death age from the description
//   [6]  OnAdded      0x140106c20  latch the spawn cell, size the wander box,
//                                  then run one Update(t=0) through slot 9
//   [9]  Update       0x140106420  the 452-instruction per-tick body
//   [11] Slot11       0x140106dd0  post-load: age scale, re-pen penned livestock
//
// plus the three private helpers Update leans on:
//
//   AnimalComponent_FindHabitat     0x140106020  expanding ring scan for a free
//                                                terrain-type-2 (grass) cell
//   AnimalComponent_SetWanderTimer  0x140106250  play an idle animation, arm the
//                                                move timer, pick a facing
//   AnimalComponent_UpdateBehavior  0x140107320  age -> mesh visual scale
//
// AnimalComponent_SetBounds (0x140105e20) is NOT re-ported here: it already has a
// faithful port as PastureComponent_AnimalSetBounds (pasturecomponent.cpp) and is
// registered by reg_component_gap_D.cpp:71; we call that body directly.
//
// ---- Struct fields (size 0x58, ctor 0x140106fc0) --------------------------
//   +0x10 Entity IBP (owner)          +0x18 AnimalDescription (Object::Base)
//   +0x20 component flag word         +0x28 int32  habitat cell index
//   +0x30 int64 Time  move timer      +0x38 short  wander box x0
//   +0x3a short wander box y0         +0x3c short  wander box width
//   +0x3e short wander box height     +0x40 uint8  "wanders" enable
//   +0x44 float age (hours)           +0x48 float  age of death (hours)
//   +0x50 int64 Time  graze timer
//
//   flag bits read by Update: 20 = penned/immobile (skip pathing entirely),
//   21 = graze latch (set once the graze timer first expires), 22 = habitat is
//   revalidated each repath, 23 = grazing enabled.
//
// ---- AnimalDescription fields (type id DAT_1401c6fd0) ---------------------
//   +0x20 int   wander box size (tiles)   +0x28 float lifespan base (hours)
//   +0x2c float lifespan variance          +0x30 float adult age (hours)
//   +0x34 float graze interval (hours)     +0x3c uint8 picks a random facing
//   +0x40 float wander retry interval (hours)
//
// ---- CharacterComponent fields (found via 0x1400012d0) -------------------
//   +0x18 CharacterDescription   +0x28 uint8 facing-override
//   +0x2c..+0x34 facing vector   +0x3c float heading
//   +0x48 action stack           +0x60 int stack-top index (-1 == empty)
//   +0x68 ModelComponent IBP -> +0x134..+0x140 Vector4 visual scale,
//                               +0x158/+0x15c/+0x160 world position
//
// Decompiled from Application-x64.exe using Ghidra.

#include "err_line.h"   // HITCH_POSTDRAIN: one write per diagnostic line
#include "Component/AnimalComponent.h"
#include <cstdint>
#include "Component/AnimalDescription.h"      // ENDGAME B.3.9 C1: 0x1401c6fd0 cell class
#include "Action/MoveTo.h"   // 0x1400c9170, by name
#include "Component/CharacterDescription.h"   // ENDGAME B.3.9 C1: 0x1401c70b0 cell class
#include "Component/CharacterComponent.h"
#include "Component/LivestockComponent.h"   // FindComponent<LivestockComponent> (0x14002f1d0)
#include "Component/ModelComponent.h"
#include "Component/TradeComponent.h"         // FindComponent<TradeComponent> (0x140015140)
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Game/Map.h"
#include "graphics/Mesh.h"
#include <new>
#include "object/TypeInfo.h"
#include "Entity/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "math/Math.h"
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Component/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Game/exe_bodies.h"
#include "Game/exe_constants.h"
#include "system/MemoryAllocator.h"

// ============================================================
// .rdata float constants (exact retail bit patterns, exe_constants.h).
// ============================================================
namespace {

inline float BitsToFloat(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

// 0x1401720b0 = 0x3991A2B4 ~ 1/3600 — seconds -> age-hours conversion.
// 0x14017225c = 0x45610000 = 3600.0  — age-hours -> seconds.
// 0x14017210c = 0x3EA8F5C3 = 0.33    — visual scale of a newborn.
// 0x140172120 = 0x3F2B851E = 0.67    — visual scale gained by adulthood.
// 0x14017214c = 0x3F800000 = 1.0     — visual scale ceiling.
// 0x140172110 = 0x3ECCCCCD = 0.4     — wander destination jitter radius.
// 0x140172114 = 0x3F000000 = 0.5     — cell-centre bias.
// 0x140172280 = 0xBF800000 = -1.0    — MoveTo "use the mover's own speed".
inline float AgeRate()    { return BitsToFloat(kRdata_1401720b0); }
inline float HoursToSec() { return BitsToFloat(kRdata_14017225c); }
inline float ScaleBase()  { return BitsToFloat(kRdata_14017210c); }
inline float ScaleGain()  { return BitsToFloat(kRdata_140172120); }
inline float ScaleMax()   { return kRdata_14017214c; }
inline float Jitter()     { return kRdata_140172110; }
inline float HalfCell()   { return kRdata_140172114; }
inline float MoveSpeed()  { return kRdata_140172280; }

// --- exe sub-callees, called directly -------------------------------------
//   0x1400012d0 ActMv_FindComponentFamily_12d0 (CharacterComponent)
//   0x140003f60 ActCNR_FindComponent_3f60      (ModelComponent)
//   0x1400c2ea0 ActHunt_MarkEntityRemoval_c2ea0
//   0x140118950 GrowthComponent_ModifyFlags
//   0x140105e20 PastureComponent_AnimalSetBounds
//   0x1401250f0 ActMvDep_AnimHelper_50f0       (set animation)
//   0x1400c5400 WorkBase_TaskListCleanup_c5400 (action stack clear)
//   0x1400c54e0 Pf_ActStackPush_54e0
//   0x1400c9170 Pf_MoveToCtor_9170
// 0x140003110 was Collection::Array<void*>::SetSize(n, fill) monomorphised into
// the exe (ENDGAME B.3.2 step 2); ActHunt_DynArraySetCount_3110 confirmed the
// 3-arg fill form matches SetSizeCore exactly, so the call site below is the
// typed array's own SetSizeFill(n, &entity).

} // namespace

namespace {

// The map grid lives at gameWorld+0xf8. Its cell array is at grid+0x80 (Ghidra
// renders that as `*(int*)(grid + 0x20)` because the pointer is typed int* —
// ELEMENTS, not bytes). Cell stride is 0x30: +0x00 occupant IBP, +0x29 terrain
// type (0x02 == the grass an animal will stand on).
// (The cell's +0x00 entity is what Map.h names MapCell::blocker.)
inline Map* MapGrid() {
    return GameUpdateLoop::_singleton ? GameUpdateLoop::_singleton->m_game : nullptr;
}
inline MapCell* CellArray(Map* grid) {
    return grid->cells.GetData();
}
inline uint8_t CellTerrain(const MapCell* cells, int64_t index) {
    return cells[index].terrainType;
}
inline ComponentEntity* CellOccupant(const MapCell* cells, int64_t index) {
    return cells[index].blocker.Get();
}
inline System::Random* Rng() {
    return GameUpdateLoop::_singleton ? GameUpdateLoop::_singleton->m_random : nullptr;
}

// Resolve self->_description as an AnimalDescription (the retail IsDerivedFrom guard).
inline AnimalDescription* AnimalDesc(AnimalComponent* self) {
    Description* raw = self->_description;
    if (!raw) return nullptr;
    return ::Object::IsDerivedFromNullSafe(raw, AnimalDescription::TypeId()) ? static_cast<AnimalDescription*>(raw) : nullptr;
}

// A model's world position and visual scale live in its MeshInstance / Transform.
inline Math::Transform* ModelTransform(ModelComponent* model) {
    return &model->transform;
}

inline int ClampCell(int v, int dim) {
    if (v < 0) v = 0;
    if (v >= dim) v = dim - 1;
    return v;
}

// --- rate-limited liveness tracing (stderr -> standalone_error.log) ---------
int s_traceFirstUpdate = 0;
int s_traceMoveTo      = 0;
int s_traceMissing     = 0;
constexpr int kTraceCap = 24;

} // namespace

// ============================================================
// AnimalComponent::Startup (vtable slot [4], 0x140106d50)
// ------------------------------------------------------------
// Rolls this animal's age of death once, at creation:
//   deathAge = desc->lifespanBase + RandFloatTol() * desc->lifespanVariance
// RandFloatTol is symmetric in [-1,1), so the lifespan straddles the base.
//
// This is load-bearing for Update: the inert factory zeroes the body, so an
// animal that never runs Startup has deathAge == 0 and dies on its first tick.
// ============================================================

// ============================================================
// AnimalComponent::FindHabitat (0x140106020)
// ------------------------------------------------------------
// Expanding-ring search around the animal's current cell for a free grass tile.
// Ring side length grows 5, 7, 9, ... and only the ring's PERIMETER is tested
// (top+bottom rows first, then the left+right columns of the interior). Gives up
// at side length > 30 and returns the original cell unchanged.
//
// A candidate qualifies when terrain == 0x02 AND the cell has no occupant.
// ============================================================
int AnimalComponent::FindHabitat() {
    Map* grid = MapGrid();
    if (!grid) return 0;

    const int startCell = habitatCell;
    int side = 3;

    for (;;) {
        int dim = grid->size;
        side += 2;
        if (dim <= 0) return startCell;

        const short sideS = static_cast<short>(side);
        const short half  = static_cast<short>(side / 2);
        short y0 = static_cast<short>(startCell / dim) - half;
        short x0 = static_cast<short>(startCell % dim) - half;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;

        // Width and height are clamped independently, both against the unclamped
        // side length (retail keeps `side` live in the register while writing the
        // clamped width to a second one).
        short w = sideS;
        if (dim <= sideS + static_cast<int>(x0)) w = static_cast<short>(static_cast<short>(dim) - x0) - 1;
        short h = sideS;
        if (dim <= sideS + static_cast<int>(y0)) h = static_cast<short>(static_cast<short>(dim) - y0) - 1;

        const int xMax = static_cast<int>(x0) + w - 1;
        const int yMax = static_cast<int>(y0) + h - 1;

        MapCell* const cells = CellArray(grid);
        if (!cells) return startCell;

        // Top and bottom edges.
        for (int x = x0; x <= xMax; ++x) {
            int64_t top = static_cast<int64_t>(y0) * grid->size + x;
            if (CellTerrain(cells, top) == 0x02 && CellOccupant(cells, top) == 0)
                return static_cast<int>(y0) * grid->size + x;
            int64_t bottom = static_cast<int64_t>(grid->size) * yMax + x;
            if (CellTerrain(cells, bottom) == 0x02 && CellOccupant(cells, bottom) == 0)
                return grid->size * yMax + x;
        }

        // Left and right edges, interior rows only.
        for (int y = static_cast<int>(y0) + 1; y <= yMax - 1; ++y) {
            int64_t left = static_cast<int64_t>(grid->size) * y + x0;
            if (CellTerrain(cells, left) == 0x02 && CellOccupant(cells, left) == 0)
                return static_cast<int>(x0) + grid->size * y;
            int64_t right = static_cast<int64_t>(grid->size) * y + xMax;
            if (CellTerrain(cells, right) == 0x02 && CellOccupant(cells, right) == 0)
                return y * grid->size + xMax;
        }

        if (side > 0x1e) return startCell;
    }
}

// ============================================================
// AnimalComponent::UpdateBehavior (0x140107320)
// ------------------------------------------------------------
// Maps age onto the mesh's visual scale so a fawn grows into a deer:
//   scale = min(1.0, age / desc->adultAge * 0.67 + 0.33)
// The scalar is broadcast into a Vector4 and stored at MeshInstance+0x10c
// (the model's +0x134).
// ============================================================
void AnimalComponent::UpdateBehavior() {
    AnimalDescription* desc = AnimalDesc(this);
    if (!desc) return;                       // retail trusts the type check; see notes

    const float adultAge = desc->_f30;
    if (adultAge == 0.0f) return;

    float scale = (age / adultAge) * ScaleGain() + ScaleBase();
    if (scale > ScaleMax()) scale = ScaleMax();

    ComponentEntity* entity = _entity.Get();
    if (!entity) return;

    CharacterComponent* character = entity->FindComponent<CharacterComponent>();   // 0x1400012d0
    if (!character) return;

    ModelComponent* model = character->modelComponent.Get();
    if (!model) return;

    const Math::Scalar scaleS(scale);
    Math::Vector4 vec4(scaleS);   // Vector4(const Scalar&) splat, slot 0x140152958
    Graphics::MeshInstance* mesh = &model->meshInstance;
    mesh->_color[0] = vec4.x;
    mesh->_color[1] = vec4.y;
    mesh->_color[2] = vec4.z;
    mesh->_color[3] = vec4.w;
}

// ============================================================
// AnimalComponent::SetWanderTimer (0x140106250)
// ------------------------------------------------------------
// The "stand still for a while" path. Plays idle animation `animId` on the
// model, arms the move timer at +0x30 with `seconds`, and — when the description
// says this species picks a random facing (desc+0x3c) — points the character at a
// random normalized direction and records the matching heading.
//
// Note the retail double-normalize: the random 2D vector is normalized into a
// scratch, then the scratch's XY are re-normalized into the character's facing.
// Reproduced as-is (the second pass is a no-op on an already-unit 2D vector).
// ============================================================
void AnimalComponent::SetWanderTimer(uint32_t animId, float seconds) {
    ComponentEntity* entity = _entity.Get();
    if (!entity) return;

    CharacterComponent* character = entity->FindComponent<CharacterComponent>();   // 0x1400012d0
    ModelComponent* model = entity->FindComponent<ModelComponent>();                // 0x140003f60

    if (!character) return;

    Description* charDescRaw = character->_description;
    CharacterDescription* charDesc = nullptr;
    if (charDescRaw && ::Object::IsDerivedFromNullSafe(charDescRaw, CharacterDescription::TypeId())) {
        charDesc = static_cast<CharacterDescription*>(charDescRaw);
    }

    if (model && charDesc) {
        // ActMvDep_AnimHelper_50f0 is a forwarder: its body is PlayModelAnimation.
        model->PlayAnimation(animId, 0.0f,
                           charDesc->_turnFilter, ScaleMax());   // 0x1401250f0
    }

    RtSystem_Time_Ctor(&moveTimer, seconds);

    AnimalDescription* desc = AnimalDesc(this);
    if (!desc || !desc->_b3c) return;

    System::Random* rng = Rng();
    if (!rng) return;

    const float r1 = rng->RandFloatTol();
    const float r0 = rng->RandFloatTol();
    const Math::Vector3 src(r0, r1, 0.0f);

    Math::Vector3 dir = src.Normalize();

    character->field_0x28 = false;

    const Math::Vector3 src2(dir.x, dir.y, 0.0f);
    Math::Vector3 facing = src2.Normalize();
    character->field_0x2C.x = facing.x;
    character->field_0x2C.y = facing.y;
    character->field_0x2C.z = facing.z;

    character->field_0x3C = dir.Heading();
}

// ============================================================
// AnimalComponent::Update (vtable slot [9], 0x140106420)
// ------------------------------------------------------------
//  1. Age the animal; past its rolled death age, mark the entity for removal and
//     append it to the world's corpse list (gameWorld+0xe8, array at +0xd0 /
//     count at +0xd8). Bit 10 of the entity's flag word means "already removed".
//  2. Graze latch: while flag bit 21 is clear and bit 23 is set, run down the
//     graze timer at +0x50. A juvenile (age <= adultAge) or an already-running
//     timer just ticks; an adult with an idle timer re-arms it to a randomized
//     (1 + rand) * desc->grazeInterval hours. On expiry, set bit 21 (the latch)
//     and zero the timer.
//  3. Refresh the visual scale from age.
//  4. Bail if the character already has a queued action, or if the move timer at
//     +0x30 has not run out yet.
//  5. Pick a destination. Penned animals (bit 20) never path. Otherwise: if bit
//     22 is set, revalidate the habitat cell (must be terrain 2 with no building)
//     and re-size the wander box. Then, if the animal has left its box OR a 1-in-4
//     roll comes up, search for a new destination — up to 100 tries of a random
//     tile inside the box, terrain-resolved and jittered by +/-0.4, re-running
//     FindHabitat every 11 consecutive misses. On success, clear the action stack
//     and push a 0x480-byte MoveTo toward the point; on failure, idle via
//     SetWanderTimer.
// ============================================================

// ============================================================
// AnimalComponent::OnAdded (vtable slot [6], 0x140106c20)
// ------------------------------------------------------------
// Register-time one-shot, dispatched by ComponentEntity_OnAddedPass. Only runs
// when flag bit 22 is set. Latches the spawn tile from the model's world
// position, sizes the wander box from desc->wanderSize, then runs a single
// Update with a zero delta (retail calls vtable+0x48, i.e. slot 9).
// ============================================================

// ============================================================================
// ENDGAME B.3.8 CIT — AnimalComponent as a REAL class (AnimalComponent.h).
//
// Before: a 48-slot hand-built AnimalComponent_LiveVtable filled at every create
// (slots 3 Serialize, 4 Startup, 6 OnAdded, 9 Update), Inert_ScalarDtor in slot
// 0. After: the compiler's 16-slot vtable and a real ctor/dtor chain (B38_RULES
// D1, D2).
// ============================================================================
#include "Object/factory_registration.h"


// 0x140106fc0. Component() is the CALL 0x1401188b0; the 0x140162110 stamp is
// the compiler's; +0x28 (dword), +0x30, +0x38, +0x40 (byte), +0x44 (one qword
// store over age and deathAge, H7), +0x50 = 0; flags |= 0xc9.
AnimalComponent::AnimalComponent()
    : habitatCell(0), _pad2C{}, moveTimer{0}, wanderX(0), wanderY(0), wanderWidth(0),
      wanderHeight(0), canGraze(false), _pad41{}, age(0.0f), deathAge(0.0f), _pad4C{},
      grazeTimer{0} {
    OrFlagsInCtor(0xc9);
}

void AnimalComponent::Startup() {
    AnimalDescription* desc = AnimalDesc(this);
    System::Random* rng  = Rng();
    if (!desc || !rng) {
        if (s_traceMissing++ < kTraceCap) {
            ErrLine("[AnimalUpd] Startup skipped: desc=%p rng=%p this=%p\n",
                    static_cast<void*>(desc), static_cast<void*>(rng), static_cast<void*>(this));
        }
        return;
    }

    float tol = rng->RandFloatTol();
    deathAge = tol * desc->_f2c + desc->_f28;
}
void AnimalComponent::OnAdded() {
    const uint32_t flags = _flags;
    if (((flags >> 0x16) & 1) == 0) return;

    ComponentEntity* entity = _entity.Get();
    if (!entity) return;

    CharacterComponent* character = entity->FindComponent<CharacterComponent>();   // 0x1400012d0
    if (!character) return;

    ModelComponent* model = character->modelComponent.Get();
    if (!model) return;

    Map* grid = MapGrid();
    if (!grid) return;
    const int dim = grid->size;
    if (dim <= 0) return;

    const float half = static_cast<float>(dim >> 1);
    const int x = ClampCell(static_cast<int>(ModelTransform(model)->translation.x + half), dim);
    const int y = ClampCell(static_cast<int>(ModelTransform(model)->translation.y + half), dim);
    habitatCell = dim * y + x;

    AnimalDescription* desc = AnimalDesc(this);
    if (desc) {
        GetWanderBox(&wanderX, &wanderY, &wanderWidth, &wanderHeight,
                                         desc->_i20);
    }

    // Retail: System::Time::Time(&t, 0.0f) then (*vtable[9])(this, &t). Slot 9 is
    // this component's own Update.
    System::Time zeroTime{0};
    RtSystem_Time_Ctor(&zeroTime, 0.0f);
    Update(&zeroTime);   // slot 9, virtual as retail
}

// [9] 0x140106420. RDX is a pointer to the elapsed System::Time (EntityList
// bucket tick 0x1400c42af).
void AnimalComponent::Update(const System::Time* time) {
    const System::Time* deltaTime = time;
    if (!deltaTime) return;

    // ---- 1. aging + death ------------------------------------------------
    const float dtSeconds = deltaTime->AsSeconds();
    const float prevAge = age;
    const float newAge = dtSeconds * AgeRate() + prevAge;
    age = newAge;

    const float death = deathAge;
    if (newAge >= death && newAge != death) {
        ComponentEntity* entity = _entity.Get();
        EntityList* const corpseOwner = GameUpdateLoop::_singleton
                                        ? GameUpdateLoop::_singleton->m_entityList : nullptr;
        if (entity == nullptr) return;

        if ((entity->_entityFlags >> 10) & 1) return;

        entity->MarkForRemoval();
        if (corpseOwner) {
            const int newCount = corpseOwner->removalList._size + 1;
            corpseOwner->removalList
                .SetSize(newCount, entity);
        }
        return;
    }

    System::Random* rng  = Rng();
    AnimalDescription* desc = AnimalDesc(this);
    if (!desc) {
        if (s_traceMissing++ < kTraceCap) {
            ErrLine("[AnimalUpd] no AnimalDescription on this=%p (raw=%p) — tick skipped\n",
                    static_cast<void*>(this), static_cast<void*>(_description));
        }
        return;
    }

    // prevAge == 0 only on this animal's very first tick (Startup seeds +0x48,
    // never +0x44, and the inert factory zeroes the body).
    if (prevAge == 0.0f && s_traceFirstUpdate++ < kTraceCap) {
        ErrLine("[AnimalUpd] first tick: this=%p desc=%p deathAge=%.2f flags=0x%08x cell=%d\n",
                static_cast<void*>(this), static_cast<void*>(desc), death,
                _flags, habitatCell);
    }

    const uint32_t flags = _flags;

    // ---- 2. graze latch --------------------------------------------------
    if (canGraze && ((flags >> 0x15) & 1) == 0 && ((flags >> 0x17) & 1) != 0) {
        const int64_t timer = grazeTimer._ticks;
        float seconds = 0.0f;
        bool rearm = true;

        if (timer != 0 || newAge <= desc->_f30) {
            if (timer < 1) {
                rearm = false;                       // nothing to run down
            } else {
                const int64_t remaining = timer - deltaTime->_ticks;
                grazeTimer._ticks = remaining;
                if (remaining > 0) {
                    rearm = false;
                } else {
                    ModifyFlags(0x200000, 0);   // 0x140118950
                    seconds = 0.0f;
                }
            }
        } else {
            const float interval = desc->_f34;
            const float roll = rng ? rng->RandFloat() : 0.0f;
            seconds = (roll * interval + interval) * HoursToSec();
        }

        if (rearm) {
            RtSystem_Time_Ctor(&grazeTimer, seconds);
        }
    }

    // ---- 3. visual scale -------------------------------------------------
    UpdateBehavior();

    // ---- 4. busy / timer gates -------------------------------------------
    ComponentEntity* entity = _entity.Get();
    if (!entity) return;

    CharacterComponent* character = entity->FindComponent<CharacterComponent>();   // 0x1400012d0
    if (!character) {
        if (s_traceMissing++ < kTraceCap) {
            ErrLine("[AnimalUpd] no CharacterComponent on entity=%p — pathing skipped\n",
                    static_cast<void*>(entity));
        }
        return;
    }

    if (character->actionStack.current + 1 > 0) return;  // action queued

    moveTimer._ticks -= deltaTime->_ticks;
    if (moveTimer._ticks > 0) return;

    // ---- 5. destination search -------------------------------------------
    Map* grid = MapGrid();
    if (!grid) return;

    // Retail re-reads the flag word here (the graze block above may have set
    // bit 21 through ModifyFlags), so do not reuse the cached copy.
    const uint32_t liveFlags = _flags;
    uint32_t wanderDir = 1;

    if (((liveFlags >> 0x14) & 1) == 0) {      // not penned
        if (((liveFlags >> 0x16) & 1) != 0) {  // habitat is revalidated per repath
            MapCell* const cells = CellArray(grid);
            const int cell = habitatCell;
            ComponentEntity* occupant = cells ? CellOccupant(cells, cell) : nullptr;

            bool blocked = false;
            if (occupant != nullptr) {
                TradeComponent* buildingIBP = occupant->FindComponent<TradeComponent>();
                // ⚠ fn_FindComponent_Building is a MISLABEL — retail 0x140015140
                // matches DWORD_1401c4b80, which is TradeComponent, not any notion of
                // "building". Verified at instruction level, and retail's own animal
                // update (0x140106420) calls exactly this function at 0x140106650.
                // So the real rule is: a TRADING POST on the tile does not disqualify
                // it; every other occupant does. That also bounds the blast radius of
                // BANISHED_LIVE_TRADE_TIDS on animal pathing — the only tiles whose
                // verdict changes when 0x1401c4b80 is registered are trading-post
                // tiles. See reference_misnamed_component_finders.
                blocked = (buildingIBP == nullptr);
            }

            if ((cells && CellTerrain(cells, cell) != 0x02) || blocked) {
                habitatCell = FindHabitat();
                GetWanderBox(&wanderX, &wanderY, &wanderWidth, &wanderHeight,
                                                 desc->_i20);
            }
        }

        // Current tile from the model's world position.
        ModelComponent* model = character->modelComponent.Get();
        if (!model) return;

        const int dim = grid->size;
        if (dim <= 0) return;
        const float halfDim = static_cast<float>(dim >> 1);
        int curX = ClampCell(static_cast<int>(ModelTransform(model)->translation.x + halfDim), dim);
        int curY = ClampCell(static_cast<int>(ModelTransform(model)->translation.y + halfDim), dim);

        const short boxX = wanderX;
        const short boxY = wanderY;
        const short boxW = wanderWidth;
        const short boxH = wanderHeight;

        const bool outOfBox =
            (((liveFlags >> 0x14) & 1) == 0 && curX < boxX) || curY < boxY ||
            static_cast<int>(boxW) + static_cast<int>(boxX) <= curX ||
            static_cast<int>(boxH) + static_cast<int>(boxY) <= curY;

        bool repath = outOfBox;
        if (!repath) {
            // NOTE: retail keeps the roll in the same register it later passes as
            // the idle-animation id, so a non-zero roll becomes the wander dir.
            wanderDir = rng ? rng->Rand(4u) : 1;
            repath = (wanderDir == 0);
        }

        // DEVIATION (bring-up guard, not retail): Random::Rand(0) divides by the
        // range, so a wander box that was never sized — OnAdded skipped because
        // flag bit 22 is clear, or SetBounds unresolved — would fault inside the
        // search loop. Retail always has a sized box here.
        if (repath && (boxW <= 0 || boxH <= 0)) {
            if (s_traceMissing++ < kTraceCap) {
                ErrLine("[AnimalUpd] wander box unsized (w=%d h=%d) on this=%p — idling\n",
                        static_cast<int>(boxW), static_cast<int>(boxH), static_cast<void*>(this));
            }
            repath = false;
            wanderDir = 1;
        }

        if (repath) {
            (void)_entity->FindComponent<ModelComponent>();   // 0x140003f60, result discarded (retail)

            const float jitter   = Jitter();
            const float halfCell = HalfCell();
            float dest[3] = {0.0f, 0.0f, 0.0f};

            int misses   = 0;   // consecutive misses since the last habitat re-find
            int attempts = 0;   // total attempts, capped at 100
            bool valid   = false;

            do {
                Map* g = MapGrid();
                if (!g) return;
                const int d = g->size;
                if (d <= 0) return;

                const uint32_t ry = rng ? rng->Rand(static_cast<uint32_t>(wanderHeight)) : 0;
                const short   by = wanderY;
                const uint32_t rx = rng ? rng->Rand(static_cast<uint32_t>(wanderWidth)) : 0;
                const int candidate =
                    static_cast<int>(rx) + wanderX +
                    (static_cast<int>(ry) + static_cast<int>(by)) * d;

                const float bias = static_cast<float>(d >> 1) - halfCell;
                float tile[3] = {static_cast<float>(candidate % d) - bias,
                                 static_cast<float>(candidate / d) - bias,
                                 0.0f};

                float world[3] = {0.0f, 0.0f, 0.0f};
                g->GetTerrainPos(world, tile, '\0', nullptr);   // 0x14009f3e0
                dest[0] = world[0];
                dest[1] = world[1];
                dest[2] = world[2];

                const float jy = rng ? rng->RandFloatTol() : 0.0f;
                const float jx = rng ? rng->RandFloatTol() : 0.0f;
                dest[0] += jx * jitter;
                dest[1] += jy * jitter;

                if (misses > 10) {
                    habitatCell = FindHabitat();
                    GetWanderBox(&wanderX, &wanderY, &wanderWidth, &wanderHeight,
                                                     desc->_i20);
                    misses = 0;
                }
                ++misses;
                ++attempts;

                const int dim2 = grid->size;
                const float half2 = static_cast<float>(dim2 >> 1);
                const int cx = ClampCell(static_cast<int>(dest[0] + half2), dim2);
                const int cy = ClampCell(static_cast<int>(dest[1] + half2), dim2);
                MapCell* const cells = CellArray(grid);
                valid = cells && CellTerrain(cells, static_cast<int64_t>(cy) * dim2 + cx) == 0x02;
            } while (!valid && attempts < 100);

            // Retail re-derives the final cell from the freshly-read grid rather
            // than reusing the loop's last test.
            Map* g2 = MapGrid();
            const int dim3 = g2 ? g2->size : 0;
            bool accept = false;
            if (dim3 > 0) {
                const float half3 = static_cast<float>(dim3 >> 1);
                const int fx = ClampCell(static_cast<int>(dest[0] + half3), dim3);
                const int fy = ClampCell(static_cast<int>(dest[1] + half3), dim3);
                MapCell* const cells2 = CellArray(g2);
                accept = cells2 && CellTerrain(cells2, static_cast<int64_t>(fy) * dim3 + fx) == 0x02;
            }

            if (accept) {
                ActionStack* stack = &character->actionStack;
                stack->Clear();

                Action* action = nullptr;
                void* mem = (::System::MemoryAllocator::_singleton)
                                ? ::System::MemoryAllocator::MemAlloc(0x480, 8)
                                : nullptr;
                if (mem) {
                    CharacterComponent* moverIBP = character;
                    action = ::new (mem) MoveTo(&moverIBP, 0 /*Normal*/, 1,
                                                dest, 0xf9f, 1, MoveSpeed());   // 0x1400c9170
                }
                // If the alloc fails, `action` stays null and we push a null IBP —
                // the same degradation retail takes when MemAlloc fails
                // (0x140106a8e: MOV RAX,RDI with RDI == 0).

                ::Object::InternalPtr<Action> actionIBP = action;
                ActionStackPush(stack, &actionIBP);
                moveTimer._ticks = 0;

                if (s_traceMoveTo++ < kTraceCap) {
                    ErrLine("[AnimalUpd] MoveTo: this=%p dest=(%.2f, %.2f, %.2f) attempts=%d\n",
                            static_cast<void*>(this), dest[0], dest[1], dest[2], attempts);
                }
                return;
            }

            // No walkable destination after 100 tries — idle and try again later.
            wanderDir = (rng ? rng->Rand(3u) : 0) + 1;
        }
    }

    const float retry = desc->_f40;
    const float roll = rng ? Rng()->RandFloat() : 0.0f;
    SetWanderTimer(wanderDir, roll * retry + retry);
}

// [11] 0x140106dd0 (104 B): the post-load fixup (EntityList's post-load pass
// 0x1400c4730 calls slot 11 on every component). Re-apply the age scale to the
// mesh (0x140107320), then, while flag bit 22 (0x400000, the bit PenInPasture
// clears and ReleaseToWander sets) is clear, re-pen
// the livestock in its pasture: FindComponent<LivestockComponent> (0x14002f1d0)
// and PenInPasture (0x1401240a0), which copies the pasture footprint into the
// wander box. Retail calls the finder twice (test, then use); one call is the
// same value, the finder has no side effect.
void AnimalComponent::Slot11() {
    UpdateBehavior();
    if (((_flags >> 0x16) & 1) != 0) return;
    LivestockComponent* livestock = _entity->FindComponent<LivestockComponent>();
    if (livestock != nullptr) {
        livestock->PenInPasture();
    }
}

// create 0x14002f020: MemAlloc(0x58, 8), then the ctor.
::Object::Base* AnimalComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(AnimalComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) AnimalComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* AnimalComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) AnimalComponent());
}

DEFINE_EXE_TYPE(AnimalComponent, "Component")
