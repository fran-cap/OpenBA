// src/diag/trade_buy_harness.cpp
//
// BANISHED_TRADE_SELL=<interval frames> — THE VIRTUAL MERCHANT CUSTOMER.
//
// A merchant arrives and docks on its own: the trade controller arms a ~6,583s timer
// at state-2 enter and "[TradeCtrl] event 9: MERCHANT DOCKED" fires on schedule
// (~12,000 frames at 50x with a dock-reachable post). What never happens is the
// TRADE, because in retail a person opens the post's panel, drags the spinners and
// clicks Trade. Nothing in an unattended run does that, so units-traded
// (townMgr+0x1ac, the value stat 13 reads) stays at exactly 0 for the life of the
// world however many merchants come and go.
//
// Six achievements sit behind that one missing click -- Trader (50,000 units),
// MasterTrader (100,000), Exports, Farmer, Food Variety and Foodie -- so this drives
// it, through the real ported path rather than by poking the counter:
//
//   1. find a trading post whose TradeUI panel can be opened (the same EditTool
//      select an in-world click uses, borrowed from the tradepanel harness),
//   2. fill every SELL row's pending quantity from the post's own stock,
//   3. call TradeUI_UpdateSellTotals / _UpdateBuyTotals so the total ELEMENTS carry
//      the numbers the checkout reads (it takes the sell total off the
//      numberSellTotal element at self+0x100 field +0xd8, not from a recompute --
//      writing the row quantities alone would credit nothing),
//   4. call TradeUI_OnTradeButton (retail 0x140081420) — the ported checkout, which
//      does the whole two-phase commit including crediting townMgr+0x1ac.
//
// So the harness supplies the intent a player would supply; every number that moves
// afterwards is computed by ported retail code.
//
// Opt-in, off by default, and it prints on every failure branch — a silent log means
// it never ran, not that it traded nothing.

#include "object/TypeInfo.h"
#include "Dialog/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Entity/ComponentEntity.h"   // B4: the component array by name
#include "Entity/EntityList.h"        // B4: GameUpdateLoop::m_entityList
#include "Entity/CitizenBehavior.h"   // B4: m_citizenList fields
#include "Component/ComponentDescription.h"
#include "options.h"
#include "Object/factory_registration.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "ext/registry.h"   // Ext::Fix().autotrade_free_goods (the autotrade watch's report)
#include "Component/StorageComponent.h"
#include "Component/TradeComponent.h"
#include "Component/BoatComponent.h"   // the trade watch: the merchant boat's route
#include "Component/ModelComponent.h"
#include "Component/StateMachineComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/CharacterDescription.h"
#include "Game/Map.h"
#include "Dialog/TradeUI.h"
#include "Action/exe_bodies.h"   // StorageAdd (0x14012d000)
#include "ui/DialogInstance.h"   // B4: the open dialog's controller array
// The sell/buy tables are built ONCE when the panel opens. Seeding the trade
// stock afterwards leaves the rows as they were, so the refresh has to be
// re-run explicitly -- otherwise the tree fills and sellRows stays 0 forever.

// edittool.cpp — activate the EditTool and drive the select path the way an
// in-world click does. HarnessSelectEntity is the wrapper (it builds the
// InternalBasePtr the impl expects); calling EditTool_SelectEntity_Impl with a
// bare entity pointer instead faults on a -1 read inside the tool.
#include "Player/edittool_funcs.h"    // EditTool_HarnessAcquire / _HarnessSelectEntity
// tradecomponent_serialize.cpp — insert/top-up one {resource, qty} pair in the
// TradeComponent's +0x60 AVL tree, the tree the sell table is built from.
// LEFT: a local declaration; the definition has no header (tradecomponent_serialize.cpp, COMP-8).
extern "C" void TradeStock_SeedForTest(TradeComponent* tradeComp, ComponentDescription* resourceEntity, int qty);

namespace {

int  s_interval = -2;      // -2 = env unread, -1 = disabled
int  s_perRow   = 0;       // max units per sell row per visit (0 = whole row)
long long s_totalSold = 0;
int  s_visits   = 0;

void Init() {
    if (s_interval != -2) return;
    const char* e = Opt().tradeSell;
    if (!e || !e[0] || e[0] == '0') { s_interval = -1; return; }
    const int v = std::atoi(e);
    s_interval = (v > 1) ? v : 1200;
    if (const char* r = Opt().tradeSellPerRow) s_perRow = std::atoi(r);
    std::fprintf(stderr, "[TradeSell] armed: every %d frames, per-row cap %d "
                         "(0 = sell the whole row)\n", s_interval, s_perRow);
    std::fflush(stderr);
}

Component* FindCompOnEntity(ComponentEntity* ent, uint32_t tid) {
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

}  // namespace

// BANISHED_TRADE_WATCH=<period> — THE MERCHANT BOAT CHECK (tools/harness_route.sh trade).
//
// TradeComponent::TickIdle (0x14005aa60) clones the merchant boat, routes it to the post
// through its BoatComponent (read unguarded) and asks for state 0x12; the boat posts
// event 9 when it ties up (OnMerchantDocked -> state 0x13, whose enter builds the manifest
// at +0x50 and arms the 900 s stay at +0x78). Every period this finds the first trading
// post, cuts the first merchant countdown (+0x78, ~6,600 game seconds) to one tick -- the
// one lever -- and then follows the boat: spawned (+0x80 set), its route (BoatComponent
// +0x60 count) and waypoint (+0x70), docked (a manifest while the merchant is set), gone
// (+0x80 clear again). [TradeWatch] PASS once a boat that was seen sailing has docked.
namespace {
int  s_watchPeriod = -2;      // -2 = unread, -1 = off
bool s_watchCut    = false;
bool s_watchSailed = false;
bool s_watchDocked = false;
ComponentEntity* s_watchBoat = nullptr;
bool s_watchAutoTrade = false;   // BANISHED_TRADE_WATCH=<period>,autotrade

// The stored units of `res` in `storage` (count, not weight).
int StoredUnits(const StorageComponent* storage, const ComponentDescription* res) {
    for (int i = 0; i < storage->storageSlots._size; ++i) {
        if (storage->storageSlots._data[i]._resource == res) return storage->storageSlots._data[i]._count;
    }
    return 0;
}

// BANISHED_TRADE_WATCH=<period>,autotrade (tools/harness_route.sh autotrade, retail_bugs §24): once the
// merchant has docked, put 20 units of a good the merchant prices 0 and 20 of one he prices above 0
// into the post's storage, ask 3 units of every buy-order category, and run the ported
// TradeComponent::AutoTrade (0x140059ad0) once. Retail's payment walk sells the priced-0 stack whole;
// fix.autotrade_free_goods keeps it. The orders are put back afterwards.
void TradeWatchAutoTrade(int frameNum, TradeComponent* post) {
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    CitizenBehavior* town = gw ? gw->m_citizenList : nullptr;
    StorageComponent* storage = post->_entity->FindComponent<StorageComponent>();
    if (!town || !storage) {
        std::fprintf(stderr, "[TradeWatch] AUTOTRADE ERROR f=%d no town list or no post storage\n", frameNum);
        return;
    }
    ComponentDescription* freeGood = nullptr;
    ComponentDescription* paidGood = nullptr;
    for (int i = 0; i < town->field_0x110._size; ++i) {
        ComponentDescription* t = town->field_0x110._data[i];
        if (!t || !storage->AcceptsMaterial(t)) continue;
        const int price = post->GetResourcePrice(t);
        if (price <= 0 && !freeGood) freeGood = t;
        if (price > 0 && !paidGood) paidGood = t;
    }
    if (!freeGood || !paidGood) {
        std::fprintf(stderr, "[TradeWatch] AUTOTRADE ERROR f=%d no priced-0 (%p) or priced (%p) good the post "
                     "stores\n", frameNum, static_cast<void*>(freeGood), static_cast<void*>(paidGood));
        return;
    }
    storage->AddItem(freeGood, 20);   // 0x14012d000
    storage->AddItem(paidGood, 20);
    std::vector<int> saved;
    for (int o = 0; o < post->buyOrders._size; ++o) {
        saved.push_back(post->buyOrders._data[o].amount);
        post->buyOrders._data[o].amount = 3;
    }
    const int freeBefore = StoredUnits(storage, freeGood);
    const int paidBefore = StoredUnits(storage, paidGood);
    const int volumeBefore = storage->QueryCount();
    post->AutoTrade();   // 0x140059ad0
    for (int o = 0; o < post->buyOrders._size && o < static_cast<int>(saved.size()); ++o) {
        post->buyOrders._data[o].amount = saved[o];
    }
    const int freeAfter = StoredUnits(storage, freeGood);
    const int paidAfter = StoredUnits(storage, paidGood);
    std::fprintf(stderr, "[TradeWatch] AUTOTRADE f=%d fix=%d volume %d -> %d of %d; priced-0 good %p (price %d) "
                 "%d -> %d; priced good %p (price %d) %d -> %d\n", frameNum, Ext::Fix().autotrade_free_goods ? 1 : 0,
                 volumeBefore, storage->QueryCount(), storage->capacity, static_cast<void*>(freeGood),
                 post->GetResourcePrice(freeGood), freeBefore, freeAfter, static_cast<void*>(paidGood),
                 post->GetResourcePrice(paidGood), paidBefore, paidAfter);
    if (freeAfter == freeBefore)
        std::fprintf(stderr, "[TradeWatch] AUTOTRADE KEPT the priced-0 stack\n");
    else
        std::fprintf(stderr, "[TradeWatch] AUTOTRADE GAVE AWAY %d priced-0 unit(s)\n", freeBefore - freeAfter);
}

void TradeWatch(int frameNum) {
    if (s_watchPeriod == -2) {
        const char* e = Opt().tradeWatch;
        s_watchPeriod = (e && e[0] && e[0] != '0') ? std::atoi(e) : -1;
        s_watchAutoTrade = e && std::strstr(e, ",autotrade") != nullptr;
        if (s_watchPeriod > 0) {
            std::fprintf(stderr, "[TradeWatch] armed: every %d frames\n", s_watchPeriod);
            std::fflush(stderr);
        }
    }
    if (s_watchPeriod <= 0 || frameNum == 0 || (frameNum % s_watchPeriod) != 0) return;
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    if (!gw || !gw->m_entityList) return;
    EntityList* el = gw->m_entityList;
    TradeComponent* post = nullptr;
    for (int i = 0; i < el->masterArray._size && !post; ++i) {
        ComponentEntity* ent = el->masterArray._data[i].Get();
        if (ent) post = ent->FindComponent<TradeComponent>();
    }
    if (!post) {
        std::fprintf(stderr, "[TradeWatch] f=%d no trading post in the world\n", frameNum);
        std::fflush(stderr);
        return;
    }
    ComponentEntity* boat = post->merchant.Get();                       // +0x80
    BoatComponent* bc = boat ? boat->FindComponent<BoatComponent>() : nullptr;
    std::fprintf(stderr, "[TradeWatch] f=%d post=%p timer(+0x78)=%lld merchant(+0x80)=%p route=%d "
                 "waypoint=%d manifest(+0x58)=%d\n", frameNum, static_cast<void*>(post),
                 static_cast<long long>(post->merchantTimer._ticks), static_cast<void*>(boat),
                 bc ? bc->route._size : -1, bc ? bc->routeIndex : -1, post->manifest._size);
    if (bc) {
        // Where the boat is against its dock (+0x40) and waypoint (+0x50), and which state
        // its machine runs (table index: 0 sail, 1 approach, 2 docked, 3 leave, 4 gone).
        StateMachineComponent* bsm = boat->FindComponent<StateMachineComponent>();
        ModelComponent* bm = bc->model.Get();
        const Math::Vector3 p = bm ? bm->transform.translation : Math::Vector3(0.0f, 0.0f, 0.0f);
        std::fprintf(stderr, "[TradeWatch]   boat state[%d] pos (%.2f,%.2f,%.2f) dock (%.2f,%.2f,%.2f) "
                     "waypoint (%.2f,%.2f)\n", bsm ? bsm->stateMachine.activeState : -9, p.x, p.y, p.z,
                     bc->dockPosition.x, bc->dockPosition.y, bc->dockPosition.z, bc->waypoint.x,
                     bc->waypoint.y);
        // The boat's height rule (the move tick 0x1401094c0): fixed z (+0x3c / +0x40) or
        // the terrain height clamped up to +0x44; and the terrain under it.
        CharacterComponent* ch = bc->character.Get();
        const CharacterDescription* cd = ch ? ch->characterDescription : nullptr;
        float tri[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float q[3] = {p.x, p.y, 0.0f};
        const float* tp = gw->m_game->GetTerrainPos(tri, q, '\0', nullptr);
        std::fprintf(stderr, "[TradeWatch]   boat height rule: fixed(+0x3c)=%d z(+0x40)=%.3f floor(+0x44)=%.3f "
                     "terrain under it %.3f\n", cd ? cd->_b3c : -1, cd ? cd->_f40 : 0.0f,
                     cd ? cd->_f44 : 0.0f, tp[2]);
    }
    if (!s_watchCut && !boat && post->merchantTimer._ticks > 1) {
        // The post's own placement (its model translation): the dock's z is this z plus
        // the "boat" attach point's local z, and the boat docks only within 0.2 of it in 3D.
        ModelComponent* pm = post->_entity->FindComponent<ModelComponent>();
        if (pm)
            std::fprintf(stderr, "[TradeWatch]   post model translation (%.3f,%.3f,%.3f)\n",
                         pm->transform.translation.x, pm->transform.translation.y,
                         pm->transform.translation.z);
        s_watchCut = true;
        post->merchantTimer._ticks = 1;
        std::fprintf(stderr, "[TradeWatch]   cut the merchant countdown to one tick\n");
    }
    if (boat && bc && bc->route._size > 0 && post->manifest._size == 0 && !s_watchSailed) {
        s_watchSailed = true;
        s_watchBoat = boat;
        std::fprintf(stderr, "[TradeWatch] f=%d merchant boat %p sailing: %d route points\n",
                     frameNum, static_cast<void*>(boat), bc->route._size);
    }
    if (boat && boat == s_watchBoat && post->manifest._size > 0 && !s_watchDocked) {
        s_watchDocked = true;
        std::fprintf(stderr, "[TradeWatch] PASS f=%d merchant boat %p docked: manifest of %d, "
                     "stay timer %lld\n", frameNum, static_cast<void*>(boat), post->manifest._size,
                     static_cast<long long>(post->merchantTimer._ticks));
        if (s_watchAutoTrade) TradeWatchAutoTrade(frameNum, post);
    }
    if (!boat && s_watchDocked && s_watchBoat) {
        std::fprintf(stderr, "[TradeWatch] f=%d merchant boat %p has left the post\n", frameNum,
                     static_cast<void*>(s_watchBoat));
        s_watchBoat = nullptr;
    }
    std::fflush(stderr);
}
}  // namespace

// The panel's row arrays are not filled by the select itself: TradeUI builds them in
// its own per-frame update, so reading self+0x30/+0x38 in the same tick that opened
// the dialog always finds sellCount = 0 however well stocked the post is. Open on
// one tick, act a few ticks later with the dialog still up.
static int s_openedAtFrame = -1;
static const int kSettleFrames = 30;

// The tool manager (gameupdateloop.cpp hands its `m_toolManager`; its local
// declaration still spells the parameter void*).
extern "C" void TradeSellHarness_Tick(PlayerToolManager* entityUI, int frameNum) {
    TradeWatch(frameNum);
    Init();
    if (s_interval <= 0 || frameNum == 0) return;
    if (!entityUI) return;
    const bool openTick = (frameNum % s_interval) == 0;
    const bool actTick  = (s_openedAtFrame >= 0 && frameNum - s_openedAtFrame >= kSettleFrames);
    if (!openTick && !actTick) return;

    GameUpdateLoop* gw = (GameUpdateLoop::_singleton != nullptr) ? GameUpdateLoop::_singleton : nullptr;
    if (!gw) return;
    CitizenBehavior* townMgr = gw->m_citizenList;   // +0xf0
    if (!townMgr) return;

    // Scan the entity list for a trading post (an entity carrying a TradeComponent).
    const uint32_t tidTrade = TradeComponent::TypeId();
    EntityList* entityList = gw->m_entityList;   // +0xe8
    Object::InternalPtr<ComponentEntity>* arr = entityList ? entityList->masterArray._data : nullptr;   // +0x08
    const int n = entityList ? entityList->masterArray._size : 0;   // +0x10
    if (!arr || n <= 0 || !tidTrade) {
        std::fprintf(stderr, "[TradeSell] f=%d cannot scan (arr=%p n=%d tid=%u)\n",
                     frameNum, static_cast<void*>(arr), n, tidTrade);
        std::fflush(stderr);
        return;
    }

    ComponentEntity* postEnt = nullptr;
    for (int i = 0; i < n && !postEnt; ++i) {
        ComponentEntity* ent = arr[i].Get();
        if (ent && FindCompOnEntity(ent, tidTrade)) postEnt = ent;
    }
    if (!postEnt) {
        std::fprintf(stderr, "[TradeSell] f=%d no trading post in the world\n", frameNum);
        std::fflush(stderr);
        return;
    }

    EditTool* tool = EditTool_HarnessAcquire(entityUI);
    if (!tool) { std::fprintf(stderr, "[TradeSell] f=%d EditTool activation FAILED\n", frameNum);
                 std::fflush(stderr); return; }
    if (openTick) {
        EditTool_HarnessSelectEntity(tool, postEnt);
        s_openedAtFrame = frameNum;
        return;                     // let the panel populate its rows first
    }
    s_openedAtFrame = -1;
    UI::DialogInstance* dialog = tool->entityRef;   // +0x28, the open dialog

    if (!dialog) { std::fprintf(stderr, "[TradeSell] f=%d post opened no dialog\n", frameNum);
                   std::fflush(stderr); return; }

    const uint32_t tidTradeUI = TradeUI::TypeId();
    UI::DialogController** ctrlArr = dialog->_controllers.GetData();   // +0x78
    const int cnt  = dialog->_controllers.GetSize();                   // +0x80
    TradeUI* tui = nullptr;
    for (int i = 0; ctrlArr && i < cnt; ++i) {
        UI::DialogController* c = ctrlArr[i];
        if (!c) continue;
        if (c->Type() == tidTradeUI) tui = static_cast<TradeUI*>(c);   // exact type id
    }
    if (!tui) {
        std::fprintf(stderr, "[TradeSell] f=%d panel opened but no TradeUI controller\n", frameNum);
        std::fflush(stderr);
        return;
    }

    const int creditBefore = townMgr->field_0x1AC;   // +0x1ac

    // Fill the SELL rows. self+0x30 array, count +0x38, stride 0x20, pending qty at
    // entry+0x18 — the same layout the checkout consumes. entry+0x10 is the row's
    // available stock (what the panel shows as sellable); cap the pending quantity to
    // it so the checkout never subtracts more than the post holds.
    int rowsFilled = 0, unitsQueued = 0;
    const int sellCount = tui->_sellItems.GetSize();                    // +0x38
    TradeUI::SellItem* const sellBase = tui->_sellItems.GetData();      // +0x30 (stride 0x20)
    // The row is {resEntity, valueDesc, resTypeComp, pendingQty} -- 0x20 bytes, the
    // shape RefreshSellTable pushes. entry+0x10 is resTypeComp, a COMPONENT POINTER,
    // not an available-stock count: reading it as one and skipping rows where it was 0
    // skipped every row whose resource carries no RawMaterialDescription, which is why
    // a table with 7 populated rows still queued nothing. The sellable amount lives in
    // the trade stock tree, and the checkout clamps against the post's storage anyway,
    // so queue a fixed per-row amount and let the ported checkout be the judge.
    const int perRow = (s_perRow > 0) ? s_perRow : 100;
    for (int i = 0; i < sellCount && sellBase; ++i) {
        sellBase[i].quantity = perRow;                                  // entry+0x18
        unitsQueued += perRow;
        ++rowsFilled;
    }

    if (rowsFilled == 0) {
        // Nothing to sell yet. The post's sell table is built from the post's OWN
        // StorageComponent, and the stress town stocks barns and yards, not the post --
        // in retail a hauler fills it only after the player marks goods for trade, which
        // is a second thing nobody clicks in an unattended run.
        //
        // BANISHED_TRADE_SELL_SEED=<n> puts n of every town raw-material template into
        // the post through AddItem (retail 0x14012d000), the same faithful storage-add
        // the founding-house food seed uses. It stands in for the hauling, not for the
        // trade: the sale itself still goes through the ported checkout on the next tick.
        int seedQty = 0;
        if (const char* sq = Opt().tradeSellSeed) seedQty = std::atoi(sq);
        int seeded = 0;
        if (seedQty > 0) {
            // Seed the TRADE stock, not the post's StorageComponent.
            //
            // TradeUI builds its sell rows from fn_FindComponent_Building(entity) +0x60 --
            // and that finder is MISNAMED: DAT_1401c4b80 is the TradeComponent, so the
            // tree is the post's TRADE stock, not its storage. Seeding the
            // StorageComponent instead (and reading storage+0x60 as a tree) produced a
            // successful-looking AddItem every tick, sellRows stuck at 0, and finally a
            // fault walking a field that was never a tree.
            // Found by exact type id.
            TradeComponent* tradeComp = static_cast<TradeComponent*>(FindCompOnEntity(postEnt, tidTrade));
            ComponentDescription** tmpls = townMgr->field_0x110._data;   // +0x110
            const int tmplN = townMgr->field_0x110._size;                 // +0x118
            if (tradeComp && tmpls && tmplN > 0) {
                for (int i = 0; i < tmplN; ++i) {
                    if (!tmpls[i]) continue;
                    TradeStock_SeedForTest(tradeComp, tmpls[i], seedQty);
                    ++seeded;
                }
            }
            // ALSO stock the post's StorageComponent. Exports reads stat 14 filtered to
            // Template\TradingPost.rsc, and stat 14 (dispatcher case 0xe) sums STORAGE
            // quantities, not the trade tree -- so the two seeds serve two different
            // achievements and both are needed. AddItem is retail 0x14012d000, the same
            // faithful storage-add the founding-house food seed uses.
            {
                // Found by exact type id.
                StorageComponent* storage = static_cast<StorageComponent*>(
                    FindCompOnEntity(postEnt, StorageComponent::TypeId()));
                if (storage && tmpls) {
                    for (int i = 0; i < tmplN; ++i)
                        if (tmpls[i])
                            storage->AddItem(tmpls[i], seedQty);   // 0x14012d000
                }
            }
            std::fprintf(stderr, "[TradeSell] f=%d seeded %d/%d resource(s) x%d into the "
                                 "TRADE stock (tradeComp=%p tree root=%p count=%d)\n",
                         frameNum, seeded, tmplN, seedQty, static_cast<void*>(tradeComp),
                         tradeComp ? static_cast<void*>(tradeComp->stock.root) : nullptr,   // +0x60
                         tradeComp ? tradeComp->stock.count : -1);                          // +0x68
        }
        if (seeded > 0) {
            tui->RefreshSellTable();
            tui->RefreshBuyTable();
            const int after = tui->_sellItems.GetSize();                // +0x38
            std::fprintf(stderr, "[TradeSell] f=%d refreshed after seeding: sellRows %d -> %d\n",
                         frameNum, sellCount, after);
            std::fflush(stderr);
            if (after > 0) return;   // next interval sells them
        }
        std::fprintf(stderr, "[TradeSell] f=%d TradeUI=%p sellRows=%d but every row has 0 "
                             "stock — nothing to sell%s\n",
                     frameNum, static_cast<void*>(tui), sellCount,
                     seeded ? " even after refresh" : " (set BANISHED_TRADE_SELL_SEED=<n>)");
        std::fflush(stderr);
        return;
    }

    // The checkout reads the sell TOTAL off the numberSellTotal element, not from the
    // rows, so the totals have to be recomputed before it runs or the credit is 0.
    tui->UpdateSellTotals();
    tui->UpdateBuyTotals();

    tui->OnTradeButton();

    const int creditAfter = townMgr->field_0x1AC;    // +0x1ac
    s_totalSold += (creditAfter - creditBefore);
    ++s_visits;
    std::fprintf(stderr, "[TradeSell] f=%d visit=%d rows=%d/%d queued=%d units -> credit "
                         "%d -> %d (+%d), cumulative +%lld  [stat 13 reads this]\n",
                 frameNum, s_visits, rowsFilled, sellCount, unitsQueued,
                 creditBefore, creditAfter, creditAfter - creditBefore,
                 (long long)s_totalSold);
    std::fflush(stderr);
}
