// src/diag/cropfield_force_start.cpp
// BANISHED_CROPFIELD_FORCE_START — switch a demo crop field ON and (optionally) fire
// its plant command, so states 9/10/11 are reachable without the drag tool or panel.
//
// WHY: CropFieldComponent_Ctor seeds +0x48 = 0, and state-2 enter publishes that byte
// into the workplace's accepting flag while the state-2 per-frame planting trigger
// returns immediately unless it is set. So a freshly spawned demo field parks in state 2
// forever — correctly. In retail the player drags the field out and picks a crop in the
// panel, which is what sets it. This is the crop-field analogue of
// BANISHED_ORCHARD_FORCE_START and, like it, only pokes already-published state.
//
// Values:
//   1  set +0x48 = 1 and let the SEASONAL trigger fire the plant on its own. This is the
//      faithful path and also tests the trigger itself, but it only fires inside the
//      year window the trigger checks, so it can wait a long time.
//   2  also post node event 3 (the panel's plant command) immediately — the same event
//      the state table registers for state 2 — so state 9 is entered on the spot.
//
// Test-only, opt-in, default off.

#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Entity/ComponentEntity.h"   // B4: the component array by name
#include "Entity/EntityList.h"        // B4: GameUpdateLoop::m_entityList
#include "Entity/CitizenBehavior.h"     // B4: m_citizenList->field_0x120
#include "Component/ComponentDescription.h"
#include "Object/factory_registration.h"
#include "Component/CropFieldComponent.h"
#include "Component/NaturalResourceDescription.h"
#include "Component/StateMachineComponent.h"
#include "Component/BuildingState.h"
#include "options.h"
#include "clamped_atoi.h"     // review F-087: checked parse of BANISHED_* text
#include "object/TypeInfo.h"
#include "object/VSlot.h"       // Object::SlotAddressOf
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr int kStartFrame = 300;
constexpr int kRetry      = 120;
constexpr int kMaxTracked = 8;

bool  s_init = false;
int   s_mode = 0;
void* s_done[kMaxTracked];
int   s_doneN = 0;

void Init() {
    if (s_init) return;
    s_init = true;
    const char* e = Opt().cropFieldForceStart;
    if (!e || !e[0] || std::strcmp(e, "0") == 0) {
        std::fprintf(stderr, "[CropStart] disabled (BANISHED_CROPFIELD_FORCE_START unset)\n");
        std::fflush(stderr);
        return;
    }
    s_mode = HarnessAtoi(e);
    if (s_mode <= 0) s_mode = 1;
    std::fprintf(stderr, "[CropStart] ENABLED mode=%d — switch demo crop fields on%s\n",
                 s_mode, s_mode >= 2 ? " and post the plant command" : "");
    std::fflush(stderr);
}

bool AlreadyDone(void* c) {
    for (int i = 0; i < s_doneN; ++i) if (s_done[i] == c) return true;
    return false;
}

Component* FindComp(ComponentEntity* ent, uint32_t tid) {
    if (!ent || !tid) return nullptr;
    const int n = ent->_components._size;                          // +0x38
    Object::InternalPtr<Component>* a = ent->_components._data;        // +0x30
    for (int i = 0; i < n && a; ++i) {
        Component* c = a[i].Get();
        if (!c) continue;
        if (c->Type() == tid) return c;
    }
    return nullptr;
}

// Post a node event through the entity's StateMachineComponent, exactly as
// OrchardComponent_Command does: smc+0x48 is the SM, sm+0x00 the node, node vtable
// slot 2 is PostEvent(node, sm, curIdx, eventId), and smc+0x51 carries the index.
void PostNodeEvent(StateMachineComponent* smc, int eventId) {
    if (!smc) return;
    StateMachine* sm = &smc->stateMachine;          // +0x48
    ControllerComponent::Controller* node = sm->current;   // sm+0x00
    if (!node) return;
    // The harness's "PostEvent is filled" test, on the slot the member names.
    if (!::Object::SlotAddressOf(node, &ControllerComponent::Controller::PostEvent)) return;
    const int curIdx = sm->activeState;             // +0x51
    node->PostEvent(sm, curIdx, eventId);
    std::fprintf(stderr, "[CropStart]   posted node event %d (node=%p curIdx=%d)\n",
                 eventId, static_cast<void*>(node), curIdx);
}

void Fire(int frame) {
    GameUpdateLoop* gw = (GameUpdateLoop::_singleton != nullptr) ? GameUpdateLoop::_singleton : nullptr;
    if (!gw) return;
    EntityList* el = gw->m_entityList;                 // +0xe8
    if (!el) return;
    Object::InternalPtr<ComponentEntity>* arr = el->masterArray._data;      // +0x08
    const int n = el->masterArray._size;               // +0x10
    const uint32_t tidCrop = CropFieldComponent::TypeId();
    const uint32_t tidSM   = StateMachineComponent::TypeId();
    if (!arr || n <= 0 || !tidCrop) return;

    int found = 0;
    for (int i = 0; i < n; ++i) {
        ComponentEntity* ent = arr[i].Get();
        if (!ent) continue;
        Component* comp = FindComp(ent, tidCrop);
        if (!comp) continue;
        ++found;
        if (AlreadyDone(comp)) continue;

        CropFieldComponent* c = static_cast<CropFieldComponent*>(comp);   // exact type id

        // ── pick a CROP TYPE (the analogue of the orchard's tree-type combo) ──────
        // State-9 enter reads comp+0x50 as a description LIST and looks inside it for a
        // NaturalResourceDescription (0x1401c7b68); that entry IS the crop. A null
        // +0x50 means "no crop chosen", so the state clears +0x49, marks no cells and
        // the field stays bare — correct for an unconfigured field, and exactly what a
        // freshly spawned demo field is.
        //
        // The panel fills it from the world's resource-type list: town manager
        // (gw+0xf0) +0x120 array / +0x128 count, each entry carrying its
        // sub-description list at +0x28 / +0x30. So pick the first entry that actually
        // contains a NaturalResourceDescription and write it in — the same value a
        // player's combo pick would produce.
        if (c->cropDescription == nullptr) {                              // +0x50
            const uint32_t tidNatRes = NaturalResourceDescription::TypeId();
            GameUpdateLoop* gw2 = GameUpdateLoop::_singleton;
            CitizenBehavior* town = gw2 ? gw2->m_citizenList : nullptr;    // +0xf0
            ComponentDescription** types = town ? town->field_0x120._data : nullptr;   // +0x120
            const int typeN = town ? town->field_0x120._size : 0;                // +0x128
            // VARIETY, not just "a crop": collect every planable type and hand each
            // field the NEXT one, rather than the first match for all of them.
            //
            // Food Variety wants 8 different crops and 8 different orchard trees
            // harvested in a single year, and Farmer wants 8 crop seed types owned.
            // Taking types[0] for every field in the world makes both permanently
            // unreachable no matter how many fields are built -- the town grows one
            // crop, eight times over. The turn is static and advances per ASSIGNED
            // field, so the spread stays even.
            static int s_cropTurn = 0;
            ComponentDescription* chosen = nullptr;
            {
                ComponentDescription* planable[64];
                int planableN = 0;
                for (int t = 0; t < typeN && types && planableN < 64; ++t) {
                    ComponentDescription* entry = types[t];
                    if (!entry) continue;
                    auto subs = entry->_subDescs;                          // +0x28
                    const int subN = entry->_subDescCount;                // +0x30
                    for (int k = 0; k < subN && subs; ++k)
                        if (subs[k] && tidNatRes &&
                            Object::IsDerivedFromNullSafe(subs[k], tidNatRes)) {
                            planable[planableN++] = entry; break;
                        }
                }
                if (planableN > 0) {
                    chosen = planable[s_cropTurn % planableN];
                    ++s_cropTurn;
                }
            }
            if (chosen) {
                c->cropDescription = chosen;                              // +0x50
                std::fprintf(stderr, "[CropStart]   crop type set: %p (from %d world "
                             "resource types)\n", (void*)chosen, typeN);
            } else {
                std::fprintf(stderr, "[CropStart]   NO crop type available — scanned %d "
                             "world resource types, none carries a "
                             "NaturalResourceDescription. The field will plant nothing; "
                             "that is a world-data gap, not a component defect.\n", typeN);
            }
        }

        const uint8_t before = c->workEnabled;                            // +0x48
        c->workEnabled = 1;
        if (s_doneN < kMaxTracked) s_done[s_doneN++] = comp;

        std::fprintf(stderr, "[CropStart] frame=%d comp=%p master(+0x48) %u -> 1, "
                     "hasCrop(+0x49)=%u slots(+0x60)=%d\n",
                     frame, (void*)comp, (unsigned)before,
                     (unsigned)c->hasCrop,                                // +0x49
                     c->slots._size);                                     // +0x60
        if (s_mode >= 2)
            PostNodeEvent(static_cast<StateMachineComponent*>(FindComp(ent, tidSM)),
                          SmId(BuildingEvent::Plant));
        std::fflush(stderr);
    }
    if (found == 0) {
        static bool said = false;
        if (!said) {
            said = true;
            std::fprintf(stderr, "[CropStart] frame=%d: no CropFieldComponent in the world "
                         "(set BANISHED_BUILD_DEMO_CROPFIELD=1 and BANISHED_LIVE_CROPFIELD=1?)\n",
                         frame);
            std::fflush(stderr);
        }
    }
}

}  // namespace

extern "C" void CropFieldForceStart_Tick(int frameNum) {
    Init();
    if (s_mode <= 0) return;
    if (frameNum < kStartFrame) return;
    if (((frameNum - kStartFrame) % kRetry) != 0) return;
    Fire(frameNum);
}
