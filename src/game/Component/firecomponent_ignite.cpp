// src/game/Component/firecomponent_ignite.cpp
// The fire mechanic's leaf bodies — everything FireComponent::Update dispatches
// and everything the citizen bucket-brigade lands on.
//
//   0x140049610  FireComponent_Ignite            start a fire + MINT the FireWork
//   0x140049130  FireComponent_SpreadToNeighbors roll fire onto an adjacent building
//   0x1400494c0  FireComponent_Extinguish        put it out, tear the work down
//   0x140049090  FireComponent_Slot15            vtable +0x78: ONE bucket applied
//   0x1400499c0  FireComponent_sub_99c0          add/remove the fire-danger rect
//   0x14009fa80  FUN_14009fa80                   the rect stamp itself
//   (0x140053720 ParticleComponent fade-OUT and 0x140042960 audio-emitter move are
//    the TORNADO lane's ports — called here, not duplicated; see below.)
//   (0x140042c60 is AmbientEmitterComponent::PlaySlot, ambientemittercomponent.cpp.)
//   0x1400e9bd0  FireWork_Ctor(mem, entityIBP, capacity)
//
// ── How the loop closes ──────────────────────────────────────────────────────
// Ignite mints a FireWork (0x1400e9bd0) and POSTS it to the work-reservation
// ledger via CitizenAI_impl_ec60(workComp, 0x4000, 0, &workIBP). There is no
// CreateWork slot-18 factory anywhere in this lane — xrefs prove Ignite is the
// ONLY caller of the FireWork ctor. A citizen taking the job runs
// FireWork::Execute (slot 4), whose action queue ends in a SendComponentMessage
// addressed to the BURNING entity with msgId = *DAT_1401c41e0 (the FireComponent
// type id). SendComponentMessage::AbstractImpl dispatches that to the matching
// component's vtable +0x78 — i.e. FireComponent_Slot15 below, which subtracts one
// bucket's worth of Time from the fire timer.
//
// ⚠ That message id is the reason the lane was inert even where it was ported:
// 0x1401c41e0 was never registered, so work_act_perform.cpp read msgId = 0, the
// dispatcher matched no component, and the water silently went nowhere. It is
// registered by standalone/reg_fire.cpp now.
//
// Faithful, offset-by-offset ports of the Ghidra decompilations. Every constant,
// every field offset and the order of every call were transcribed from the
// listings; do not "improve" the behaviour.

#include "Work/FireWork.h"   // B.3.8 CB1a (Vtable)
#include "Component/FireDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/AmbientEmitterDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/firecomponent_funcs.h"
#include "Object/factory_registration.h"
#include "Component/FireComponent.h"
#include <new>
#include "math/Math.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"   // B3R STORAGE: g_gameSettings
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"
#include "Component/AmbientEmitterComponent.h"
#include "Component/ModelComponent.h"   // Ignite: the emitters move to the model's translation
#include <cstdint>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "audio/Audio.h"     // ENDGAME B.3.7 callers (K4)
#include "Component/FireComponent.h"
#include "system/MemoryAllocator.h"
#include "Component/exe_bodies.h"
#include "Component/tornadocomponent_funcs.h"
#include "Player/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "Game/exe_bodies.h"
#include "Game/exe_constants.h"
#include "Component/DestroyComponent.h"
#include "Component/ParticleComponent.h"
#include "Component/WorkComponent.h"
#include "Component/MapComponent.h"
#include "Game/PlayerToolManager.h"
#include "Game/GameSettings.h"
#include "Entity/ComponentEntity.h"
#include "Game/WorkScheduler.h"
#include "Game/WeatherSimulation.h"
#include "Game/EventLog.h"   // EventLog::AddEvent (0x140095580)

// EventLog::AddEvent (0x140095580) — ported by the tornado lane (eventlog.cpp),
// declared in Game/exe_bodies.h. The three shared SM-state helpers are ported once
// in firecomponent_smstate.cpp (firecomponent_funcs.h).

// ============================================================
// Dependencies, called directly
// ============================================================
//   0x140005780 DestroyComponent finder   FindComponent<DestroyComponent>
//   0x140005200 ParticleComponent finder  FindComponent<ParticleComponent>
//   0x140003050 WorkComponent finder      FindComponent<WorkComponent>
//   0x140004680 MapComponent finder       FindComponent<MapComponent>
//   0x140002280 AmbientEmitterComponent finder  FindComponent<AmbientEmitterComponent> (D.4 AUDIOCOMP)
//   0x140003f60 ModelComponent finder          FindComponent<ModelComponent>
//   0x14012ec60 CitizenAI_WorkScheduler_Post, 0x1400f9ad0 RemovalCalc_CitizenAI_9ad0,
//   0x14012eb50 WorkComponent_RegisterWorkSlot, 0x140053660 ParticleComponent_StartEmitterFade,
//   0x140053720 Tornado_ParticleReverse_53720 (tornado lane's port),
//   0x14011e710 PlayerPath_Sub_11e710.
//   .rdata: 0x140172234 = 300.0f (seconds-per-unit scale), 0x140172114 = 0.5f.
namespace {

inline GameUpdateLoop* GameWorld() {
    return GameUpdateLoop::_singleton;
}

inline int64_t MakeTime(float seconds) {
    ::System::Time t = {0};
    RtSystem_Time_Ctor(&t, seconds);
    return t._ticks;
}

}  // namespace

// ============================================================
// Type ids this lane reads out of exe .rdata.
// ============================================================

// The +0x18 description, validated the way retail validates it.
//
// Retail: `obj = this->_unk18; desc = IsDerivedFrom(obj, FireDescTid) ? obj : 0;`
// and then dereferences `desc` UNCONDITIONALLY. We keep the unconditional deref —
// the earlier `if (desc)` guard here was the bug, not the fix: with the type-id
// global unregistered it silently deleted the whole ignition roll.
//
// The member Base::IsDerivedFrom (0x1800c0390, import slot 0x140152a98) on the +0x18
// description with no null test, as every retail body that reads it does: MarkDanger
// 0x1400499c0 (0x1400499d4-0x1400499f4), Extinguish 0x1400494c0 (0x1400494e9-0x1400494f7),
// Ignite 0x140049610 (0x140049690-0x14004969d), SpreadToNeighbors 0x140049130 (its own
// 0x140049154-0x14004916e; the neighbour's 0x1400493d5-0x1400493ef, after the
// neighbour FireComponent's own test at 0x1400493b4), OnMessage 0x140049090
// (0x1400490a8-0x1400490c6). (B8 RES, row 1041: was IsDerivedFromOrTrust.)
static inline FireDescription* FireDesc(const Component* self) {
    Description* obj = self->_description;
    return obj->IsDerivedFrom(FireDescription::TypeId())
        ? static_cast<FireDescription*>(obj) : nullptr;
}

// ============================================================
// FUN_14009fa80 — stamp a value onto the fire-danger byte of a cell rect.
// ============================================================
// grid[0x20] (byte 0x80) is the fine-cell array, stride 0x30; +0x22 is the
// per-cell fire-danger counter, and grid byte 0x9c (grid[0x27] as int16) is the
// town-wide count of active fires. `delta` is +1 on ignite, -1 on extinguish.
// param_2 in the listing is dead (the caller passes yMax twice).
// Its receiver is the Map (GameUpdateLoop +0xf8): Map::StampDanger. The cell's +0x22 is
// MapCell::influenceCounts[6] (the 9 serialized bytes at +0x1C). The active-fire count is
// Map::influenceTotals[6] (+0x9c), one of the per-kind shorts StampInfluence also keeps.
void Map::StampDanger(int xMin, int yMin, int xMax, int yMax, short delta) {
    if (yMin <= yMax) {
        for (int y = yMin; y <= yMax; ++y) {
            for (int x = xMin; x <= xMax; ++x) {
                const int w = size;
                if (x >= 0 && y >= 0 && x < w && y < w) {
                    uint8_t* cell = &cells.GetData()[static_cast<int64_t>(w * y + x)].influenceCounts[6];   // +0x22
                    *cell = static_cast<uint8_t>(*cell + static_cast<char>(delta));
                }
            }
        }
    }
    influenceTotals[6] = static_cast<int16_t>(influenceTotals[6] + delta);
}

// ============================================================
// FireComponent_sub_99c0 (0x1400499c0) — add (+1) / remove (-1) the danger rect.
// ============================================================
void FireComponent::MarkDanger(short delta) {
    FireDescription* desc = FireDesc(this);

    MapComponent* map = _entity->FindComponent<MapComponent>();   // 0x140004680

    const int r = desc->dangerRadius;
    // Retail loads the footprint with MOVSX: signed shorts.
    const int16_t ox = static_cast<int16_t>(map->tileX0);
    const int16_t oy = static_cast<int16_t>(map->tileY0);
    const int16_t w  = static_cast<int16_t>(map->tileX1);
    const int16_t h  = static_cast<int16_t>(map->tileY1);
    const uint32_t yMax = static_cast<uint32_t>(oy - 1 + h + r);

    GameUpdateLoop* gw = GameWorld();
    if (!gw) return;
    Map* grid = gw->m_game;   // +0xf8
    if (!grid) return;   // the tree's (retail passes the map unguarded)
    grid->StampDanger(ox - r, oy - r, ox - 1 + w + r, static_cast<int>(yMax), delta);   // 0x14009fa80
}

// ── 0x140053720 (ParticleComponent fade-out) and 0x140042960 (move this entity's
// audio emitters) are NOT ported here. The TORNADO lane ported and registered both
// first (tornadocomponent.cpp:977 / :1002, standalone/reg_tornado.cpp:80-81) and
// its damage arm calls them for the same reason this lane does. Calling those
// bodies keeps ONE body per retail function
// (reference_one_retail_fn_ported_twice_diverges) — writing a second copy here
// would have been exactly that failure mode, with each copy faithful in a
// different half.

// ============================================================
// FireWork_Ctor (0x1400e9bd0) — the 3-arg ctor, the ONLY producer of a FireWork:
// FireWork(void* pEntityIBP, uint32_t capacity) (FireWork.h), called by name from
// Ignite below. The C wrapper around it (FireWork_CtorFull_e9bd0) is gone.
// ============================================================

// ============================================================
// FireComponent_Extinguish (0x1400494c0)
// ============================================================
void FireComponent::Extinguish() {
    fireTimer._ticks = 0;
    spreadTimer._ticks = 0;
    FireDescription* desc = FireDesc(this);

    // ParticleComponent: fade the flames out.
    {
        ParticleComponent* part = _entity->FindComponent<ParticleComponent>();   // 0x140005200
        part->FadeOutEmitter(desc->particleEmitter);   // 0x140053720
    }

    // DestroyComponent: leave the BURNING state (retail 0x14004953a-0x14004954f hands
    // the FindComponent result to 0x14011ea10 untested).
    {
        DestroyComponent* dest = _entity->FindComponent<DestroyComponent>();   // 0x140005780
        dest->StopBurning();
    }

    // WorkComponent: release the firefighting work slot (this is what retires the
    // FireWork Ignite posted).
    {
        WorkComponent* work = _entity->FindComponent<WorkComponent>();   // 0x140003050
        work->CancelWork();
    }

    // AmbientEmitterComponent: stop the crackle. Retail 0x1400494e9-0x14004953c:
    // FindComponent<AmbientEmitterComponent> (0x140002280), then slot
    // [desc->audioEmitter] of its array (both reads unguarded: every building
    // template that can burn carries an `ambientemitter` whose entry 0 is
    // Audio/Effects/FireEffect.rsc), RemoveEmitter at the emitter's position,
    // StopEffect3D when no emitter is left on the effect, handle = 0.
    {
        AmbientEmitterComponent* em = _entity->FindComponent<AmbientEmitterComponent>();
        AmbientEmitterSlot* slot = &em->slots._data[desc->audioEmitter];
        Audio::Scene* scene = GameWorld()->m_audioScene;   // [0x1401c3dd8]+0x78
        const uint32_t id = slot->handle;
        scene->RemoveEmitter(id, em->position);
        if (scene->GetEmitterCount(id) == 0)
            scene->StopEffect3D(id);
        slot->handle = 0;
    }

    MarkDanger(-1);

    // OUTCOME trace — say which of the two Slot15/Update conditions brought us here.
}

// ============================================================
// FireComponent_Ignite (0x140049610)
// ============================================================
void FireComponent::Ignite() {
    GameUpdateLoop* gw = GameWorld();
    if (!gw) return;

    // --- the two abort gates, on the DestroyComponent ---
    DestroyComponent* destroyComp = _entity->FindComponent<DestroyComponent>();   // 0x140005780

    if (destroyComp->IsBeingDemolished()) {
        return;
    }
    if ((destroyComp->InRemovalState() ? 1 : 0) != 0) {   // 0x14011e710 (the PlayerPath_Sub_11e710 forwarder's member)
        return;
    }

    FireDescription* desc = FireDesc(this);

    // Burn duration = Time(BurnScale * 300 + BurnBase); the DestroyComponent gets
    // HALF of it (its own damage clock).
    const int64_t burnFull = MakeTime(desc->burnScale * kRdata_140172234 + desc->burnBase);
    const System::Time burnFullTime = {burnFull};
    const float   burnSecs = burnFullTime.AsSeconds();
    const System::Time burnHalf = {MakeTime(burnSecs * kRdata_140172114)};

    if (!destroyComp->BeginBurning(&burnHalf)) {
        return;
    }

    fireTimer._ticks = burnFull;
    spreadTimer._ticks = MakeTime(desc->spreadInterval);
    bucketsApplied = 0;      // one qword store in the tree (+0x28 and +0x2c): H7
    bucketsAtSpread = 0;

    // ParticleComponent: fade the flames in.
    {
        ParticleComponent* part = _entity->FindComponent<ParticleComponent>();   // 0x140005200
        part->StartEmitterFade(desc->particleEmitter);
    }

    // --- MINT the FireWork and POST it to the reservation ledger ---
    WorkComponent* workComp = _entity->FindComponent<WorkComponent>();   // 0x140003050

    // FireWork's 3-argument ctor 0x1400e9bd0, called by name (the old
    // FireWork_CtorFull_e9bd0 wrapper around it is no longer called from here).
    FireWork* work = nullptr;
    if (::System::MemoryAllocator::_singleton) {
        void* mem = ::System::MemoryAllocator::MemAlloc(0x60, 8);
        if (mem) {
            ComponentEntity* entityIbp = _entity.Get();
            work = ::new (mem) FireWork(&entityIbp, static_cast<uint32_t>(desc->maxFirefighters));
        }
    }

    ::Object::InternalPtr<Work> workIbp(work);

    if (workComp) {
        workComp->PostWork(0x4000, 0, &workIbp);
    }
    if (workComp) {
        gw->m_tutorial->PrioritizeWork(workComp->workId);   // 0x1400f9ad0 (+0x118)
    }

    // --- first-fire-in-town announcement ---
    // The exe inlines EventLog::AddEvent here rather than calling 0x140095580, but
    // the inline is byte-identical to the 4-arg overload; the tornado lane's
    // eventlog.cpp port covers it. Mapping, from the inlined literals:
    //   local_80[0] = 0xFFFF6060  == SeverityColor(3)   => severity  = 3
    //   local_70    = 0x1F        == entry+0x10         => eventType = 0x1F
    //   local_6c    = 3           == entry+0x14         => severity  = 3  (agrees)
    //   local_64    = 3           == IconIndex(0x1F)    => 3          (agrees)
    //   local_60    = -1          == PopupIndex(0x1F)   => -1         (agrees)
    //   EventArray_Grow(eventLog+0x18, *(int*)(eventLog+0x20) + 1, entry)
    // All five literals reproduce, so the call is the inline.
    // The Map's active-fire count is Map::influenceTotals[6] (+0x9c).
    const Map* grid = gw->m_game;
    if (grid && grid->influenceTotals[6] < 1) {
        EventLog* eventLog = gw->m_eventNotifier;   // +0x138
        Object::InternalPtr<ComponentEntity> entityIbp(_entity);
        if (eventLog) eventLog->AddEvent(0x1f, 3, &entityIbp);
        // The settings' auto-slow byte (+0x17). Retail 0x1400498e6:
        // MOV RAX,[0x1401c3de0]; CMP byte [RAX+0x17],0 -- the byte of the settings
        // BLOCK (this read indexed the cell pointer, 0xb8 bytes past it).
        if (g_gameSettings != nullptr && g_gameSettings->autoSlow != 0) {
            PlayerToolManager* evt = gw->m_toolManager;   // +0x120
            if (evt) evt->gameSpeed = 1;
        }
    }

    // --- audio: move this entity's emitters to its world position, then start one ---
    // Retail 0x140049985-0x1400499b5: FindComponent<AmbientEmitterComponent>
    // (0x140002280), FindComponent<ModelComponent> (0x140003f60, its translation at
    // +0x158), MoveTo (0x140042960), PlaySlot(desc->audioEmitter) (0x140042c60): the
    // building's FireEffect loop (entry 0 of its `ambientemitter`, `_addOnCreate =
    // false`) starts at the model. Both finds are read unguarded, as retail does.
    {
        AmbientEmitterComponent* em = _entity->FindComponent<AmbientEmitterComponent>();
        ModelComponent* model = _entity->FindComponent<ModelComponent>();
        const Math::Vector3 pos = model->transform.translation;
        em->MoveTo(pos);
        em->PlaySlot(desc->audioEmitter);
    }

    MarkDanger(1);

}

// ============================================================
// FireComponent_SpreadToNeighbors (0x140049130)
// ============================================================
// Spread census — see the comment at the roll site below.

void FireComponent::SpreadToNeighbors() {
    GameUpdateLoop* gw = GameWorld();
    if (!gw) return;

    FireDescription* myDesc = FireDesc(this);
    const Map* grid = gw->m_game;   // +0xf8
    if (!grid) return;

    MapComponent* map = _entity->FindComponent<MapComponent>();   // 0x140004680

    // Footprint-derived sample rect, expanded by min(w,h) on each side. Retail loads
    // the shorts with MOVSX.
    int w  = static_cast<int>(static_cast<int16_t>(map->tileX1));
    int h  = static_cast<int>(static_cast<int16_t>(map->tileY1));
    const int16_t oy = static_cast<int16_t>(map->tileY0);
    const int16_t ox = static_cast<int16_t>(map->tileX0);
    const int pad = (w < h) ? w : h;
    int xMax = w - 1 + pad + ox;
    int yMax = h - 1 + pad + oy;
    int xMin = ox - pad;
    if (xMin < 0) xMin = 0;
    const int gwidth = grid->size;
    if (gwidth <= xMax) xMax = gwidth - 1;
    if (gwidth <= yMax) yMax = gwidth - 1;

    if (fireTimer._ticks < 1) {
        bucketsAtSpread = bucketsApplied;
        return;
    }

    System::Random* rng = gw->m_random;

    // Retail's `do { ... } while (fireTimer > 0)` relies on the spread interval
    // (desc+0x30 * 300) being non-zero — each pass adds it to +0x38 and the loop
    // exits as soon as +0x38 goes positive. HANG GUARD ONLY (not a behaviour
    // change): a .rsc with a zero interval would spin here forever and say nothing.
    // The cap is far above any real pass count; tripping it is a loud report.
    const int kMaxPasses = 4096;
    int passes = 0;

    do {
        if (spreadTimer._ticks > 0) break;
        if (++passes > kMaxPasses) {
            break;
        }

        spreadTimer._ticks += MakeTime(myDesc->spreadInterval * kRdata_140172234);

        int cx = rng->Rand(static_cast<int32_t>(xMin), static_cast<int32_t>(xMax));
        int cy = rng->Rand(static_cast<int32_t>(oy - pad), static_cast<int32_t>(yMax));

        // Fine-cell array at grid+0x80, stride 0x30; +0x00 is MapCell::blocker.
        ComponentEntity* occupant =
            grid->cells.GetData()[static_cast<int64_t>(grid->size) * cy + cx].blocker.Get();

        // Retail inlines the exact-id scan here (no IsDerivedFrom): the generic find.
        FireComponent* neighbourFire = nullptr;
        if (occupant != nullptr) {
            const int n = occupant->_components._size;
            for (int i = 0; i < n; ++i) {
                Component* c = occupant->_components[i].Get();
                uint32_t tid = c->Type();   // slot 2
                if (tid == FireComponent::TypeId()) {
                    neighbourFire = static_cast<FireComponent*>(c);
                    break;
                }
            }
        }

        if (occupant != nullptr && neighbourFire != nullptr) {
            FireDescription* nDesc = FireDesc(neighbourFire);

            float chance = nDesc->spreadChance;
            // Half the chance while it is raining.
            const WeatherSimulation* weather = gw->m_weather;   // +0x130
            if (weather && weather->weatherType == WeatherType::Rain) chance *= kRdata_140172114;
            // Half it again once the brigade is visibly winning here.
            if (nDesc->bucketsToOut / 5 < bucketsApplied - bucketsAtSpread)
                chance *= kRdata_140172114;

            const float roll = rng->RandFloat();
            const bool caught = roll < chance;
            if (caught) {
                neighbourFire->Ignite();
            }
        }
    } while (fireTimer._ticks > 0);

    bucketsAtSpread = bucketsApplied;
}

// ============================================================
// FireComponent_Slot15 (0x140049090) — vtable +0x78, ONE bucket of water.
// ============================================================
// Reached from SendComponentMessage::AbstractImpl with msg == 0, the last action
// in the bucket-brigade queue PerformWork_Action_CopyCtor builds.

// [15] 0x140049090 — the water-bucket message (SendComponentMessage +0x78). The
// body reads only the message id.
void FireComponent::OnMessage(int message, void*, uint32_t, uint32_t) {
    if (message != 0) return;
    FireDescription* desc = FireDesc(this);

    bucketsApplied += 1;

    const int needed = desc->bucketsToOut;
    const int64_t step = MakeTime((desc->spreadInterval * kRdata_140172234) /
                                  static_cast<float>(needed));
    fireTimer._ticks -= step;

    const int32_t buckets = bucketsApplied;
    const int64_t timer   = fireTimer._ticks;

    if (timer < 1 || needed <= buckets)
        Extinguish();
}

