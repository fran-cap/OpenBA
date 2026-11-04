// src/diag/orchard_cut_harness.cpp
// Drives the orchard panel's CUT button (command id 3) without a human click, so the
// newly-ported state 20 can be tested from `make sa-run`.
//
// This is a stand-in for the PLAYER, not for any game code: it asks CanCommand(3) exactly
// as the panel does, and then calls the real idx14 Command(comp, 3), which posts node
// event 5 -> Orchard_Event5Clear -> request state 20. Every gate downstream (the event
// list, the transition table, the state-20 handlers, the ClearArea work) still runs for
// real. The user's 2026-08-10 hand-test proved this exact call path works; the harness
// only removes the human from the loop so a cut can be observed in one automated run.
//
// Env:
//   BANISHED_ORCHARD_CUT=<frame>   enable; fires once at that frame (a bare "1" means
//                                  the default frame below, so the value reads as
//                                  "when", not "how many").
//
// Default OFF. Cutting is destructive to the orchard under test, so it must never fire
// during the plant/tend/harvest runs.
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Component/OrchardComponent.h"
#include "Entity/ComponentEntity.h"   // B4: the component array by name
#include "Entity/EntityList.h"        // B4: GameUpdateLoop::m_entityList
#include "options.h"
#include "clamped_atoi.h"     // review F-087: checked parse of BANISHED_* text
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

bool s_init = false, s_enabled = false, s_fired = false;
int  s_frame = 3000;   // late enough that a demo orchard has planted its grid

void Init() {
    if (s_init) return;
    s_init = true;
    const char* env = Opt().orchardCut;
    if (!env || !env[0] || std::strcmp(env, "0") == 0 || std::strcmp(env, "false") == 0) {
        std::fprintf(stderr, "[OrchardCut] disabled (BANISHED_ORCHARD_CUT unset)\n");
        return;
    }
    s_enabled = true;
    const int v = HarnessAtoi(env);
    if (v > 1) s_frame = v;
    std::fprintf(stderr, "[OrchardCut] ENABLED — will click CUT once at frame %d\n", s_frame);
    std::fflush(stderr);
}

}  // namespace

extern "C" void OrchardCutHarness_Tick(int frameNum) {
    Init();
    if (!s_enabled || s_fired || frameNum < s_frame) return;
    s_fired = true;

    GameUpdateLoop* gw = (GameUpdateLoop::_singleton != nullptr) ? GameUpdateLoop::_singleton : nullptr;
    EntityList* entityList = gw ? gw->m_entityList : nullptr;                     // +0xe8
    Object::InternalPtr<ComponentEntity>* arr = entityList ? entityList->masterArray._data : nullptr;   // +0x08
    const int n   = entityList ? entityList->masterArray._size : 0;               // +0x10
    const uint32_t tid = OrchardComponent::TypeId();
    if (!arr || n <= 0 || !tid) {
        std::fprintf(stderr, "[OrchardCut] frame=%d: cannot scan (arr=%p n=%d tid=%u)\n",
                     frameNum, static_cast<void*>(arr), n, tid);
        std::fflush(stderr);
        return;
    }

    int found = 0, clicked = 0;
    for (int i = 0; i < n; ++i) {
        ComponentEntity* ent = arr[i].Get();
        if (!ent) continue;
        const int cN = ent->_components._size;                     // +0x38
        Object::InternalPtr<Component>* cA = ent->_components._data;   // +0x30
        OrchardComponent* comp = nullptr;
        for (int c = 0; c < cN && cA; ++c) {
            Component* cc = cA[c].Get();
            if (!cc) continue;
            if (cc->Type() == tid) {
                comp = static_cast<OrchardComponent*>(cc);   // exact type id
                break;
            }
        }
        if (!comp) continue;
        ++found;

        // Ask before clicking, and report BOTH arms: a greyed-out CUT is itself the answer
        // to "does the button work", so a silent skip would be the worst outcome here.
        const uint8_t can = static_cast<uint8_t>(comp->CommandMask(3));
        std::fprintf(stderr, "[OrchardCut] frame=%d comp=%p CanCommand(3)=%u -> %s\n",
                     frameNum, static_cast<void*>(comp), can,
                     can ? "clicking CUT" : "SKIPPED (button greyed)");
        if (!can) continue;
        comp->Command(3);
        ++clicked;
    }
    std::fprintf(stderr, "[OrchardCut] frame=%d: %d orchard(s) found, %d CUT click(s) issued\n",
                 frameNum, found, clicked);
    std::fflush(stderr);
}
