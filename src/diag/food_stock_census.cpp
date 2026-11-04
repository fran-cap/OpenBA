// src/diag/food_stock_census.cpp
// BANISHED_FOOD_STOCK — periodic town-stock census, the generic form of the meter that
// proved the crop field's deposit.
//
// WHY: every food class in this wave ends the same way — a raw material has to appear in
// TOWN STOCK. Each class was being judged by a different proxy (slots reaching a state, a
// work Execute running, an action trace firing), and every one of those proxies produced
// at least one wrong conclusion this wave: an absent [ORGANIC-PRODUCE] line was read as
// "nothing is produced" when the producer simply took a different code path, and slot
// state 2 was read as "harvested" when it is the stale-slot drop. Stock is the outcome
// itself, so it cannot be wrong about whether food arrived.
//
// The table is the same one HarvestArea's own capacity test reads:
//   gameWorld+0xf0 -> +0x170 = entry array, stride 0x20, entry+0x00 = current stock,
//   entry+0x08 = limit.
// Only entries whose stock CHANGED since the last dump are printed, with the delta, so a
// long soak stays readable and a class that deposits nothing is visibly silent rather
// than lost in noise.
//
// Value: 1/on = dump every 600 frames, or a frame count. Test-only, opt-in.

#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Entity/CitizenBehavior.h"   // B4: m_citizenList / professionResources
#include "options.h"
#include "clamped_atoi.h"     // review F-087: checked parse of BANISHED_* text
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr int kMaxMaterials = 64;

bool s_init   = false;
int  s_period = 0;
int  s_prev[kMaxMaterials];
long long s_gross[kMaxMaterials];   // cumulative POSITIVE delta = gross deposits
bool s_seeded = false;

void Init() {
    if (s_init) return;
    s_init = true;
    const char* e = Opt().foodStock;
    if (!e || !e[0] || std::strcmp(e, "0") == 0) return;
    s_period = HarnessAtoi(e);
    if (s_period <= 1) s_period = 600;
    std::fprintf(stderr, "[FoodStock] ENABLED — town stock census every %d frames\n", s_period);
    std::fflush(stderr);
}

}  // namespace

extern "C" void FoodStockCensus_Tick(int frame) {
    Init();
    if (!s_period || (frame % s_period) != 0) return;
    if (GameUpdateLoop::_singleton == nullptr) return;

    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    CitizenBehavior* tracker = gw->m_citizenList;                     // +0xf0
    if (!tracker) return;
    const ProfessionResourceEntry* table = tracker->professionResources._data;   // +0x170
    if (!table) return;

    int changed = 0;
    for (int i = 0; i < kMaxMaterials; ++i) {
        const ProfessionResourceEntry& e = table[i];                  // stride 0x20
        const int cur = e.currentCount;                               // +0x00
        const int lim = e.maxCount;                                   // +0x08
        // Reject entries that are not real materials. Scanning a fixed 64 walks off the
        // end of the table and the tail decodes as nonsense (measured: limits like
        // -2100900192 "rising" in lockstep), which would read as a dozen materials
        // depositing at once. A real entry has a sane positive limit and a stock within
        // it. Bound by the DATA rather than by a guessed count.
        if (lim <= 0 || lim > 1000000) { s_prev[i] = cur; continue; }
        if (cur < 0 || cur > lim) { s_prev[i] = cur; continue; }
        if (!s_seeded) { s_prev[i] = cur; continue; }
        if (cur != s_prev[i]) {
            const int d = cur - s_prev[i];
            // Track GROSS deposits, not net stock. Material 5 is the aggregate Food pool,
            // so citizens eating drags the net down while production pushes it up and the
            // two cancel — a class that genuinely deposits can show a FALLING stock.
            // Summing only positive deltas isolates production, which is what makes a
            // per-class A/B against a no-building control meaningful.
            if (d > 0) s_gross[i] += d;
            std::fprintf(stderr, "[FoodStock] material %2d: %d -> %d  (%+d, limit %d) "
                         "grossDeposited=%lld\n",
                         i, s_prev[i], cur, d, lim, s_gross[i]);
            s_prev[i] = cur;
            ++changed;
        }
    }
    // Totals line every 5 intervals, so a whole run summarises to one grep even when the
    // interval deltas are noisy.
    {
        static int s_tick = 0;
        if (s_seeded && (++s_tick % 5) == 0)
            for (int i = 0; i < kMaxMaterials; ++i)
                if (s_gross[i] > 0)
                    std::fprintf(stderr, "[FoodStockTotal] material %2d grossDeposited=%lld\n",
                                 i, s_gross[i]);
    }
    if (!s_seeded) {
        s_seeded = true;
        std::fprintf(stderr, "[FoodStock] baseline captured\n");
    } else if (!changed) {
        // Print in BOTH arms: an empty census must never be mistaken for "not running".
        std::fprintf(stderr, "[FoodStock] no material changed this interval\n");
    }
    std::fflush(stderr);
}
