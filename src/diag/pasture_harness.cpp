// src/diag/pasture_harness.cpp
// BANISHED_PASTURE_FILL=<frame> — one-shot pasture stocking for the animal-wave
// test rig. At the chosen in-game frame (value "1" means the default frame 900,
// ~15s in: long past the pasture controller's state-2 enter, so AUTOTYPE has
// already assigned a livestock type), calls the ported debug-menu "fill
// pastures" body (retail 0x140060290: for every pasture in the world list with
// an assigned type, spawn animals up to capacity, 5 per pasture per call).
// Retail reaches the same body from DebugUI_Slot9; the retail PLAYER path is
// trade delivery, which standalone doesn't have yet — this is the stand-in.
// No-op unless the env var is set AND BANISHED_LIVE_PASTURE is live (the
// address only resolves when reg_pasture_live.cpp registered it).
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "options.h"
#include "clamped_atoi.h"     // review F-087: checked parse of BANISHED_* text
#include "Component/PastureComponent.h"
#include "Entity/CitizenBehavior.h"   // B4: the town pasture registry
#include "Component/exe_bodies.h"   // Pasture_FillAllPastures (0x140060290)
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// BANISHED_PASTURE_TEND=<frame interval> — herder-labor stand-in. Every N
// frames, sends message 0 to every typed pasture (Pasture_MsgReceive, retail
// slot 15), exactly what a herdsman's completed work cycle does: each message
// tags one random pastured animal breed-eligible (flag bit 23). Value "1"
// picks the default interval (300 frames ≈ 5s). Stand-in until the organic
// chain lands (PastureComponent_Slot14 cmd 2 posts PastureWork -> citizen
// herder claims it -> SendComponentMessage(0)).
static void PastureTendHarness_Tick(int frameNum) {
    static int s_interval = -2;               // -2 = env unread, -1 = disabled
    if (s_interval == -2) {
        const char* e = Opt().pastureTend;
        if (!e || !e[0] || e[0] == '0') s_interval = -1;
        else {
            int v = HarnessAtoi(e);
            s_interval = (v > 1) ? v : 300;
            fprintf(stderr, "[PastureTend] armed: msg-0 every %d frames\n", s_interval);
        }
    }
    if (s_interval < 0 || frameNum == 0 || (frameNum % s_interval) != 0) return;

    if (GameUpdateLoop::_singleton == nullptr) return;
    CitizenBehavior* gs = GameUpdateLoop::_singleton->m_citizenList;   // +0xf0
    if (!gs) return;
    int n = gs->pastures._size;                                         // +0x98
    Object::InternalPtr<PastureComponent>* list = gs->pastures._data;                           // +0x90
    if (n <= 0 || !list) return;
    for (int i = 0; i < n; ++i) {
        PastureComponent* pasture = list[i].Get();
        if (!pasture) continue;
        if (pasture->livestockType == nullptr) continue;              // +0x28
        // gameState+0x90 is the world pasture registry (pasture state 2 adds the
        // component, state 3 / slot 7 erase it): every entry is a PastureComponent.
        // Slot 15 OnMessage; the body reads only the message id.
        pasture->OnMessage(0, nullptr, 0, 0);
    }
}

// ── RETIRED 2026-08-15: BANISHED_PASTURE_HERDERS ─────────────────────────────────────
// The herder STAFFING stand-in used to live here. It called the seat cascade
// (CitizenEmploy_SeatWorkers = FindAvailableBuilding + cbf0->ca20->c5f0->c4b0) DIRECTLY,
// reaching around the UI, because the widget that normally drives that cascade — the
// workplace panel's workerCountUp click — was unproven. It is now measured end to end
// (roster 0 -> 1 on the dispatch, held 3600 frames), so the bypass is gone and pastures
// are staffed by src/diag/panel_staffing_driver.cpp along with every other workplace,
// through WorkPlaceUI slot 9 exactly as a human click would. Nothing pasture-specific
// remains: the driver keys off WPC accepting/roster/capacity, which a typed pasture sets
// via Slot14 cmd 2 just like any other employer.

// BANISHED_PASTURE_PRODUCE=<frame interval> — production-accumulator stand-in.
// LivestockComponent::Update is the only writer of pasture+0x44 and it is LIVE (traced
// arm CREDIT/BELOW_THRESHOLD under BANISHED_PASTURE_TRACE), but its interval is
// desc+0x2C * 300s == 9e8 ticks, i.e. tens of minutes of wall clock even at speed 40 —
// far too slow to exercise the production PANEL. Every N frames this credits each typed
// pasture with +8, which is what a handful of livestock intervals would have credited.
// It does not touch the drop/tracker path: PastureProduce still decides everything
// downstream, so what the panel then shows is the real code under test.
static void PastureProduceHarness_Tick(int frameNum) {
    static int s_interval = -2;
    if (s_interval == -2) {
        const char* e = Opt().pastureProduce;
        if (!e || !e[0] || e[0] == '0') s_interval = -1;
        else {
            int v = HarnessAtoi(e);
            s_interval = (v > 1) ? v : 300;
            fprintf(stderr, "[PastureProduce] armed: +8 accumulator every %d frames\n",
                    s_interval);
        }
    }
    if (s_interval < 0 || frameNum == 0 || (frameNum % s_interval) != 0) return;
    if (GameUpdateLoop::_singleton == nullptr) return;
    CitizenBehavior* gs = GameUpdateLoop::_singleton->m_citizenList;   // +0xf0
    if (!gs) return;
    int n = gs->pastures._size;                                         // +0x98
    Object::InternalPtr<PastureComponent>* list = gs->pastures._data;                           // +0x90
    if (n <= 0 || !list) return;
    for (int i = 0; i < n; ++i) {
        PastureComponent* pasture = list[i].Get();
        if (!pasture) continue;
        if (pasture->livestockType == nullptr) continue;              // +0x28
        pasture->production += 8;                                     // +0x44
    }
}

static const int kFillRetry = 300;

extern "C" void PastureFillHarness_Tick(int frameNum) {
    PastureTendHarness_Tick(frameNum);
    PastureProduceHarness_Tick(frameNum);
    static int s_fillFrame = -2;              // -2 = env unread, -1 = disabled
    if (s_fillFrame == -2) {
        const char* e = Opt().pastureFill;
        if (!e || !e[0] || e[0] == '0') s_fillFrame = -1;
        else {
            int v = HarnessAtoi(e);
            s_fillFrame = (v > 1) ? v : 900;
            fprintf(stderr, "[PastureFill] armed: fill pastures from frame %d, re-run "
                    "every %d frames (retail body only touches TYPED pastures and stops "
                    "at capacity, so this is self-limiting)\n", s_fillFrame, kFillRetry);
        }
    }
    // Same manual-play problem as the herder stand-in: a one-shot at frame 900 runs before
    // the player has picked a livestock type, and the retail fill body skips untyped
    // pastures — so nothing ever arrives. Repeat instead; it is bounded by pasture capacity.
    if (s_fillFrame < 0 || frameNum < s_fillFrame ||
        ((frameNum - s_fillFrame) % kFillRetry) != 0) return;

    int n = Pasture_FillAllPastures();   // 0x140060290
    fprintf(stderr, "[PastureFill] frame %d: fill pass ran (ret=%d) — see "
            "[Pasture] SPAWN/bind lines\n", frameNum, n);
    fflush(stderr);
}
