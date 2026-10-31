// src/diag/clear_overlap_harness.cpp
//
// BANISHED_CLEAR_OVERLAP=<startFrame>[,<stockpileDelay>[,<period>[,<deadline>[,<size>[,<hall>]]]]]
// -- the "stockpile over a clear-tool area" scenario (tools/harness_route.sh clearoverlap).
//
// The player report it reproduces: with a town hall placed over trees and rocks (four
// builders seated) and the Clear Tree / Clear Stone / Clear Iron tools dragged over an
// area, a stockpile placed OVER that area while its resources are still being removed
// stalls the town: nothing more is collected and neither the stockpile nor the town
// hall gets built.
//
// What it does, all through the player's own tool paths (no hand-built entities):
//   startFrame            : picks a resource-dense window R near the first founding
//                           house and a second one T for the town hall; places
//                           Template\TownHall.rsc over T (PlacementTool::CreateAt), then
//                           selects Template\ClearTree.rsc, ClearStone.rsc and
//                           ClearIron.rsc in that order (PlayerToolManager::SelectTool)
//                           and commits each over R (RemovalTool::CalcPath, what a drag
//                           release calls).
//   startFrame + delay    : selects Template\StorageYard.rsc and commits it over R shrunk
//                           by one cell (DragPlacementTool::CalcPath).
//   every `period` frames : a census -- the resources, clear markers and piles left in R
//                           and T, every ClearArea work over them (its entity / cell sets,
//                           each holder and whether ValidateTask still holds it), the two
//                           sites' state machines, and every laborer and builder (job id,
//                           action stack depth, top action).
//   deadline              : PASS when R holds no natural resource and both sites have
//                           finished clearing (each holds its own footprint cells); FAIL
//                           otherwise, naming what is left and whether the town's storage
//                           still takes one of the area's piles.
//   size (default 10)     : the side of R. hall (default 1): 0 places no town hall (the
//                           control: few enough piles for the start storage's free room).
//
// A diagnostic only; it changes no game state beyond what the three player clicks do.

#include "options.h"
#include "Game/GameUpdateLoop.h"
#include "Game/PlayerToolManager.h"
#include "Game/ProfessionList.h"
#include "Game/WorkScheduler.h"
#include "Game/WorkSchedulerNodes.h"
#include "Game/Map.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Entity/CitizenBehavior.h"
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"
#include "Component/ModelComponent.h"
#include "Component/MapComponent.h"
#include "Component/WorkComponent.h"
#include "Component/NaturalResourceComponent.h"
#include "Component/RawMaterialComponent.h"
#include "Component/ClearAreaComponent.h"
#include "Component/StateMachineComponent.h"
#include "Component/StorageComponent.h"
#include "Component/ComponentDescription.h"
#include "Component/CreateRemovalDescription.h"
#include "Action/Action.h"
#include "Player/Tool.h"
#include "Player/dragtool.h"
#include "Player/RemovalTool.h"
#include "Player/PlacementTool.h"
#include "Work/Work.h"
#include "Work/ClearArea.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" ComponentDescription* StartTown_ResolveTemplateByPath(const char* path);
extern "C" ComponentEntity* StartTown_GetFoundingHouseEntity(int index);
extern "C" int StartTown_GetFoundingHouseCount();
extern "C" int PlacementTool_HarnessStartupProbe(PlacementTool* self);
extern "C" int PlacementTool_HarnessPlaceAtCell(PlacementTool* self, int centerCell);

namespace {

struct Rect {
    int x0 = 0, y0 = 0, w = 0, h = 0;   // columns / rows, inclusive start, size
    bool Contains(int col, int row) const {
        return col >= x0 && col < x0 + w && row >= y0 && row < y0 + h;
    }
    bool Intersects(const Rect& o) const {
        return x0 < o.x0 + o.w && o.x0 < x0 + w && y0 < o.y0 + o.h && o.y0 < y0 + h;
    }
};

bool s_init = false;
int  s_start = -1;          // -1: disabled
int  s_delay = 900;
int  s_period = 300;
int  s_deadline = 0;        // 0: none
int  s_size = 10;
bool s_withHall = true;
bool s_placed = false;
bool s_stockpilePlaced = false;
bool s_verdict = false;

Rect s_area;                // R: the clear-tool rectangle
Rect s_yard;                // the stockpile rectangle (R shrunk by one)
Rect s_hall;                // the town hall footprint, read back from its MapComponent
ComponentDescription* s_hallTmpl = nullptr;
ComponentDescription* s_yardTmpl = nullptr;
const ComponentDescription* s_clearMarker = nullptr;   // the removal tools' marker template (+0x38)
ComponentEntity* s_hallEnt = nullptr;
ComponentEntity* s_yardEnt = nullptr;
int s_lastAreaNat = -1;
int s_flatCensuses = 0;
int s_lastHasStorage = -1;  // -1: no pile in R at the last census

void Init() {
    if (s_init) return;
    s_init = true;
    const char* e = Opt().clearOverlap;
    if (!e || !e[0] || std::strcmp(e, "0") == 0) return;
    int a = 0, b = 0, c = 0, d = 0, sz = 0, hall = 1;
    int n = std::sscanf(e, "%d,%d,%d,%d,%d,%d", &a, &b, &c, &d, &sz, &hall);
    if (n < 1 || a <= 0) return;
    s_start = a;
    if (n >= 2 && b > 0) s_delay = b;
    if (n >= 3 && c > 0) s_period = c;
    if (n >= 4 && d > 0) s_deadline = d;
    if (n >= 5 && sz >= 3) s_size = sz;
    if (n >= 6) s_withHall = hall != 0;
    std::fprintf(stderr, "[ClearOverlap] armed: start=%d stockpileDelay=%d period=%d deadline=%d\n",
                 s_start, s_delay, s_period, s_deadline);
}

Map* GetMap() {
    return GameUpdateLoop::_singleton ? GameUpdateLoop::_singleton->m_game : nullptr;
}

bool IsNatural(const ComponentEntity* e) {
    return e && const_cast<ComponentEntity*>(e)->FindComponent<NaturalResourceComponent>() != nullptr;
}

bool IsPile(const ComponentEntity* e) {
    return e && const_cast<ComponentEntity*>(e)->FindComponent<RawMaterialComponent>() != nullptr;
}

struct CellCounts {
    int natural = 0;     // occupants carrying a NaturalResourceComponent
    int piles = 0;       // occupants carrying a RawMaterialComponent (a felled log, cut stone)
    int other = 0;       // any other occupant
    int markers = 0;     // blockers that are clear-tool markers
    int claimed = 0;     // natural resources whose component carries bit 0x100000 (being removed)
};

CellCounts CountRect(const Rect& r) {
    CellCounts c;
    Map* map = GetMap();
    if (!map) return c;
    const int width = map->size;
    const MapCell* cells = map->cells.GetData();
    for (int row = r.y0; row < r.y0 + r.h; ++row) {
        for (int col = r.x0; col < r.x0 + r.w; ++col) {
            const MapCell& cell = cells[static_cast<size_t>(row * width + col)];
            ComponentEntity* occ = cell.occupant.Get();
            if (occ) {
                NaturalResourceComponent* nr = occ->FindComponent<NaturalResourceComponent>();
                if (nr) {
                    ++c.natural;
                    if ((nr->_flags >> 20) & 1) ++c.claimed;
                } else if (IsPile(occ)) {
                    ++c.piles;
                } else {
                    ++c.other;
                }
            }
            ComponentEntity* blk = cell.blocker.Get();
            if (blk && s_clearMarker && blk->_description == s_clearMarker) ++c.markers;
        }
    }
    return c;
}

// The best w x h window around (cx, cy): the most natural resources, no building or
// clear marker in it, and not overlapping `avoid`.
bool PickWindow(int cx, int cy, int w, int h, int radius, const Rect* avoid, Rect* out) {
    Map* map = GetMap();
    if (!map) return false;
    const int width = map->size;
    const MapCell* cells = map->cells.GetData();
    int best = -1;
    for (int y0 = cy - radius; y0 <= cy + radius; ++y0) {
        for (int x0 = cx - radius; x0 <= cx + radius; ++x0) {
            if (x0 < 4 || y0 < 4 || x0 + w >= width - 4 || y0 + h >= width - 4) continue;
            Rect r;
            r.x0 = x0;
            r.y0 = y0;
            r.w = w;
            r.h = h;
            if (avoid && r.Intersects(*avoid)) continue;
            int score = 0;
            bool bad = false;
            for (int row = y0; row < y0 + h && !bad; ++row) {
                for (int col = x0; col < x0 + w; ++col) {
                    const MapCell& cell = cells[static_cast<size_t>(row * width + col)];
                    if (cell.blocker.Get() != nullptr) {
                        bad = true;
                        break;
                    }
                    ComponentEntity* occ = cell.occupant.Get();
                    if (!occ) continue;
                    if (IsNatural(occ)) {
                        ++score;
                    } else {
                        bad = true;
                        break;
                    }
                }
            }
            if (bad) continue;
            if (score > best) {
                best = score;
                *out = r;
            }
        }
    }
    return best > 0;
}

const char* ActionName(const Action* a) {
    return a ? a->TypeName() : "-";
}

const char* ProfessionTag(const CitizenComponent* c) {
    ProfessionList* pl = GameUpdateLoop::_singleton->m_professionList;
    if (c->profession == pl->field_0x30) return "laborer";
    if (c->profession == pl->field_0x38) return "builder";
    return nullptr;
}

Rect RectOf(ComponentEntity* e);

bool SiteClaimed(ComponentEntity* e) {
    if (!e) return false;
    Rect r = RectOf(e);
    Map* map = GetMap();
    const MapCell* cells = map->cells.GetData();
    // Any one cell is enough: a footprint string can leave cells ('.') unoccupied.
    for (int row = r.y0; row < r.y0 + r.h; ++row) {
        for (int col = r.x0; col < r.x0 + r.w; ++col) {
            if (cells[static_cast<size_t>(row * map->size + col)].occupant.Get() == e) return true;
        }
    }
    return false;
}

ComponentEntity* FindNewestEntityOf(const ComponentDescription* tmpl) {
    EntityList* el = GameUpdateLoop::_singleton->m_entityList;
    for (int i = el->masterArray._size - 1; i >= 0; --i) {
        ComponentEntity* e = el->masterArray._data[i].Get();
        if (e && e->_description == tmpl) return e;
    }
    return nullptr;
}

Rect RectOf(ComponentEntity* e) {
    Rect r;
    MapComponent* mc = e ? e->FindComponent<MapComponent>() : nullptr;
    if (mc) {
        r.x0 = mc->tileX0;
        r.y0 = mc->tileY0;
        r.w = mc->tileX1;
        r.h = mc->tileY1;
    }
    return r;
}

int SmState(ComponentEntity* e) {
    StateMachineComponent* sm = e ? e->FindComponent<StateMachineComponent>() : nullptr;
    return sm ? sm->stateMachine.activeState : -99;
}

int ClearRemaining(ComponentEntity* e) {
    ClearAreaComponent* ca = e ? e->FindComponent<ClearAreaComponent>() : nullptr;
    return ca ? ca->clearState : -1;
}

// ---- the census ------------------------------------------------------------------

void DumpClearArea(const char* tag, ClearArea* ca, uint32_t stepCount) {
    WorkComponent* wc = ca->field_18.Get();
    ComponentEntity* owner = ca->field_10.Get();
    Rect r = RectOf(owner);
    std::fprintf(stderr,
                 "[ClearOverlap]   %s ClearArea id=%d owner=%p rect=(%d,%d %dx%d) flags=%d%d%d filter=%p "
                 "entities=%d cells=%d maxWorkers=%d visited=%d\n",
                 tag, ca->field_20, static_cast<void*>(owner), r.x0, r.y0, r.w, r.h, ca->field_70 ? 1 : 0,
                 ca->field_71 ? 1 : 0, ca->field_72 ? 1 : 0, static_cast<void*>(ca->field_68),
                 ca->_entities._count, ca->_cells._count, wc ? wc->workState : -1,
                 static_cast<uint32_t>(ca->field_2c) == stepCount ? 1 : 0);
    for (auto it = ca->_entities.Begin(); it != ca->_entities.End(); ++it) {
        CitizenComponent* cit = (*it).citizen.Get();
        bool holds = cit ? ca->ValidateTask(nullptr, &cit, nullptr) : false;
        std::fprintf(stderr, "[ClearOverlap]     entity %p -> citizen %p %s\n",
                     static_cast<void*>((*it).entity.Get()), static_cast<void*>(cit),
                     holds ? "(holds the task)" : "(STALE: the citizen holds no task of this work)");
    }
    for (auto it = ca->_cells.Begin(); it != ca->_cells.End(); ++it) {
        CitizenComponent* cit = (*it).citizen.Get();
        bool holds = cit ? ca->ValidateTask(nullptr, &cit, nullptr) : false;
        std::fprintf(stderr, "[ClearOverlap]     cell %d -> citizen %p %s\n", (*it).cell,
                     static_cast<void*>(cit),
                     holds ? "(holds the task)" : "(STALE: the citizen holds no task of this work)");
    }
}

void WalkWorks(WorkSchedulerIdNode* node, uint32_t stepCount, int* nAreaWorks, int* nOther) {
    if (!node) return;
    WalkWorks(node->left, stepCount, nAreaWorks, nOther);
    Work* w = node->work.Get();
    if (w) {
        ComponentEntity* owner = w->field_10.Get();
        Rect r = RectOf(owner);
        bool near = r.w > 0 && (r.Intersects(s_area) || (s_hall.w > 0 && r.Intersects(s_hall)));
        if (near) {
            if (w->Type() == ClearArea::TypeId()) {
                const char* tag = owner == s_yardEnt ? "YARD" : owner == s_hallEnt ? "HALL" : "tool";
                ++*nAreaWorks;
                // The per-cell tool jobs are many: only the sites and any job holding a
                // citizen are listed in full.
                ClearArea* ca = static_cast<ClearArea*>(w);
                if (owner == s_yardEnt || owner == s_hallEnt || ca->_entities._count + ca->_cells._count > 0)
                    DumpClearArea(tag, ca, stepCount);
            } else {
                ++*nOther;
                std::fprintf(stderr, "[ClearOverlap]   work %s id=%d owner=%p rect=(%d,%d %dx%d) visited=%d\n",
                             w->TypeName(), w->field_20, static_cast<void*>(owner), r.x0, r.y0, r.w, r.h,
                             static_cast<uint32_t>(w->field_2c) == stepCount ? 1 : 0);
            }
        }
    }
    WalkWorks(node->right, stepCount, nAreaWorks, nOther);
}

void Census(int frame) {
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;
    CellCounts area = CountRect(s_area);
    CellCounts hall = s_hall.w > 0 ? CountRect(s_hall) : CellCounts{};
    CellCounts yard = s_yard.w > 0 ? CountRect(s_yard) : CellCounts{};
    std::fprintf(stderr,
                 "[ClearOverlap] census f=%d area: natural=%d (claimed %d) piles=%d markers=%d other=%d | "
                 "hall: natural=%d piles=%d claimed=%d left=%d | yard: natural=%d piles=%d claimed=%d left=%d\n",
                 frame, area.natural, area.claimed, area.piles, area.markers, area.other, hall.natural, hall.piles,
                 SiteClaimed(s_hallEnt) ? 1 : 0, ClearRemaining(s_hallEnt), yard.natural, yard.piles,
                 SiteClaimed(s_yardEnt) ? 1 : 0, ClearRemaining(s_yardEnt));

    WorkScheduler* sched = gw->m_tutorial;
    int nArea = 0, nOther = 0;
    WalkWorks(sched->trees[0].root, sched->stepCount, &nArea, &nOther);
    std::fprintf(stderr, "[ClearOverlap]   %d ClearArea work(s) and %d other work(s) over the two areas\n", nArea,
                 nOther);

    CitizenBehavior* town = gw->m_citizenList;
    // The storage the haulers can drop into (CitizenBehavior +0x00 / +0x10, the two lists
    // PickBestDropTarget walks), and whether HasStorageFor takes one of the area's piles.
    const ComponentDescription* pileDesc = nullptr;
    int pileQty = 0;
    {
        Map* map = GetMap();
        const MapCell* cells = map->cells.GetData();
        for (int row = s_area.y0; row < s_area.y0 + s_area.h && !pileDesc; ++row) {
            for (int col = s_area.x0; col < s_area.x0 + s_area.w; ++col) {
                ComponentEntity* occ = cells[static_cast<size_t>(row * map->size + col)].occupant.Get();
                RawMaterialComponent* rm = occ ? occ->FindComponent<RawMaterialComponent>() : nullptr;
                if (rm) {
                    pileDesc = occ->_description;
                    pileQty = rm->quantity;
                    break;
                }
            }
        }
    }
    for (int pass = 0; pass < 2; ++pass) {
        const auto& list = pass == 0 ? town->field_0x00 : town->field_0x10;
        for (int i = 0; i < list._size; ++i) {
            ComponentEntity* e = list._data[i].Get();
            StorageComponent* st = e ? e->FindComponent<StorageComponent>() : nullptr;
            Rect r = RectOf(e);
            std::fprintf(stderr,
                         "[ClearOverlap]   storage list%d[%d] %p%s rect=(%d,%d %dx%d) sm=%d cap=%d used=%d "
                         "takesPile=%d\n",
                         pass, i, static_cast<void*>(e), e == s_yardEnt ? " (YARD)" : "", r.x0, r.y0, r.w, r.h,
                         SmState(e), st ? st->capacity : -1, st ? st->QueryCount() : -1,
                         (st && pileDesc) ? (st->CanAdd(pileDesc, pileQty) ? 1 : 0) : -1);
        }
    }
    if (pileDesc) {
        s_lastHasStorage = town->HasStorageFor(const_cast<ComponentDescription*>(pileDesc), pileQty) ? 1 : 0;
        std::fprintf(stderr, "[ClearOverlap]   HasStorageFor(area pile x%d) = %d\n", pileQty, s_lastHasStorage);
    }
    int idle = 0, busy = 0;
    for (int i = 0; i < town->citizens._size; ++i) {
        CitizenComponent* c = town->citizens._data[i].Get();
        if (!c) continue;
        const char* prof = ProfessionTag(c);
        if (!prof) continue;
        CharacterComponent* ch = c->character.Get();
        int depth = ch ? ch->actionStack.current + 1 : -1;
        const Action* top = (ch && depth > 0) ? ch->actionStack.actions[ch->actionStack.current].Get() : nullptr;
        if (depth > 0) ++busy;
        else ++idle;
        std::fprintf(stderr, "[ClearOverlap]   %s %p job=%d stack=%d top=%s\n", prof, static_cast<void*>(c),
                     c->field_0x30, depth, ActionName(top));
    }
    std::fprintf(stderr, "[ClearOverlap]   laborers+builders: %d busy, %d idle\n", busy, idle);

    if (s_stockpilePlaced) {
        if (area.natural == s_lastAreaNat) ++s_flatCensuses;
        else s_flatCensuses = 0;
        s_lastAreaNat = area.natural;
    }

    // A site has finished clearing when its footprint is its own: ClearArea state 2's
    // event-1 broadcast runs MapComponent::AddToMap over it (the state machine's
    // activeState is a table index, not the state id, so it is no test of this).
    bool done = s_stockpilePlaced && area.natural == 0 && SiteClaimed(s_yardEnt) &&
                (!s_withHall || SiteClaimed(s_hallEnt));
    if (done && !s_verdict) {
        s_verdict = true;
        std::fprintf(stderr, "[ClearOverlap] PASS f=%d: the area is cleared and both sites hold their "
                             "footprints (hall %s, yard claimed)\n",
                     frame, s_withHall ? "claimed" : "not placed");
    }
    if (!s_verdict && s_deadline > 0 && frame >= s_deadline) {
        s_verdict = true;
        std::fprintf(stderr,
                     "[ClearOverlap] FAIL f=%d: stall -- area natural=%d (unchanged for %d censuses) piles=%d "
                     "markers=%d, hall claimed=%d piles=%d, yard claimed=%d piles=%d, HasStorageFor(pile)=%d\n",
                     frame, area.natural, s_flatCensuses, area.piles, area.markers, SiteClaimed(s_hallEnt) ? 1 : 0,
                     hall.piles, SiteClaimed(s_yardEnt) ? 1 : 0, yard.piles, s_lastHasStorage);
    }
    std::fflush(stderr);
}

// ---- the three player actions -----------------------------------------------------

bool CommitRemoval(PlayerToolManager* ui, const char* path, int startCell, int endCell) {
    ComponentDescription* tmpl = StartTown_ResolveTemplateByPath(path);
    if (!tmpl) {
        std::fprintf(stderr, "[ClearOverlap] ERROR: %s does not resolve\n", path);
        return false;
    }
    ui->SelectTool(tmpl);
    Tool* tool = ui->currentTool.Get();
    RemovalTool* rt = tool && tool->Type() == RemovalTool::TypeId() ? static_cast<RemovalTool*>(tool) : nullptr;
    if (!rt || !rt->removalDesc) {
        std::fprintf(stderr, "[ClearOverlap] ERROR: %s installed tool %s, not a RemovalTool with a description\n",
                     path, tool ? tool->TypeName() : "(none)");
        return false;
    }
    if (!s_clearMarker) s_clearMarker = rt->removalDesc->_ref38;
    CellCounts before = CountRect(s_area);
    bool ok = rt->CalcPath(startCell, endCell);
    CellCounts after = CountRect(s_area);
    std::fprintf(stderr, "[ClearOverlap] %s over the area -> %d; clear markers %d -> %d\n", path, ok ? 1 : 0,
                 before.markers, after.markers);
    return ok;
}

void PlaceHallAndClear(PlayerToolManager* ui, int frame) {
    Map* map = GetMap();
    const int width = map->size;
    ComponentEntity* house = StartTown_GetFoundingHouseCount() > 0 ? StartTown_GetFoundingHouseEntity(0) : nullptr;
    Rect hr = RectOf(house);
    const int cx = house ? hr.x0 : width / 2;
    const int cy = house ? hr.y0 : width / 2;

    if (!PickWindow(cx, cy, s_size, s_size, 30, nullptr, &s_area)) {
        std::fprintf(stderr, "[ClearOverlap] ERROR: no resource window near (%d,%d)\n", cx, cy);
        return;
    }
    CellCounts ac = CountRect(s_area);
    std::fprintf(stderr, "[ClearOverlap] f=%d clear area R = (%d,%d %dx%d), %d natural resources\n", frame,
                 s_area.x0, s_area.y0, s_area.w, s_area.h, ac.natural);

    // The town hall: over trees, clear of R by a margin.
    s_hallTmpl = StartTown_ResolveTemplateByPath("Template\\TownHall.rsc");
    PlacementTool* pt = nullptr;
    if (s_hallTmpl && s_withHall) {
        ui->SelectTool(s_hallTmpl);
        Tool* t = ui->currentTool.Get();
        if (t && t->Type() == PlacementTool::TypeId()) pt = static_cast<PlacementTool*>(t);
    }
    if (pt) {
        Rect grown = s_area;
        grown.x0 -= 3;
        grown.y0 -= 3;
        grown.w += 6;
        grown.h += 6;
        Rect hallWin;
        if (PickWindow(cx, cy, 8, 8, 30, &grown, &hallWin)) {
            const int centre = (hallWin.y0 + 4) * width + (hallWin.x0 + 4);
            PlayerToolManager* realOwner = pt->dialogPtr;
            int placed = PlacementTool_HarnessPlaceAtCell(pt, centre);
            pt->dialogPtr = realOwner;
            s_hallEnt = placed ? FindNewestEntityOf(s_hallTmpl) : nullptr;
            s_hall = RectOf(s_hallEnt);
            CellCounts hc = CountRect(s_hall);
            std::fprintf(stderr, "[ClearOverlap] town hall placed=%d ent=%p rect=(%d,%d %dx%d), %d natural under it\n",
                         placed, static_cast<void*>(s_hallEnt), s_hall.x0, s_hall.y0, s_hall.w, s_hall.h, hc.natural);
        }
    } else if (s_withHall) {
        std::fprintf(stderr, "[ClearOverlap] ERROR: the town hall's PlacementTool did not install\n");
    }

    const int a = s_area.y0 * width + s_area.x0;
    const int b = (s_area.y0 + s_area.h - 1) * width + (s_area.x0 + s_area.w - 1);
    CommitRemoval(ui, "Template\\ClearTree.rsc", a, b);
    CommitRemoval(ui, "Template\\ClearStone.rsc", a, b);
    CommitRemoval(ui, "Template\\ClearIron.rsc", a, b);
    ui->SelectTool(nullptr);
}

void PlaceStockpile(PlayerToolManager* ui, int frame) {
    Map* map = GetMap();
    const int width = map->size;
    s_yard = s_area;
    s_yard.x0 += 1;
    s_yard.y0 += 1;
    s_yard.w -= 2;
    s_yard.h -= 2;
    s_yardTmpl = StartTown_ResolveTemplateByPath("Template\\StorageYard.rsc");
    if (!s_yardTmpl) {
        std::fprintf(stderr, "[ClearOverlap] ERROR: Template\\StorageYard.rsc does not resolve\n");
        return;
    }
    ui->SelectTool(s_yardTmpl);
    Tool* t = ui->currentTool.Get();
    DragTool* dt = t ? static_cast<DragTool*>(t) : nullptr;
    if (!dt) {
        std::fprintf(stderr, "[ClearOverlap] ERROR: the stockpile tool did not install\n");
        return;
    }
    CellCounts before = CountRect(s_yard);
    const int a = s_yard.y0 * width + s_yard.x0;
    const int b = (s_yard.y0 + s_yard.h - 1) * width + (s_yard.x0 + s_yard.w - 1);
    const char* toolName = t->TypeName();
    bool ok = dt->CalcPath(a, b);
    ui->SelectTool(nullptr);   // deletes the tool
    s_yardEnt = ok ? FindNewestEntityOf(s_yardTmpl) : nullptr;
    s_stockpilePlaced = ok;
    CellCounts after = CountRect(s_yard);
    std::fprintf(stderr,
                 "[ClearOverlap] f=%d stockpile (%s) over (%d,%d %dx%d) -> %d ent=%p; under it: natural %d "
                 "(claimed %d), markers %d -> %d\n",
                 frame, toolName, s_yard.x0, s_yard.y0, s_yard.w, s_yard.h, ok ? 1 : 0,
                 static_cast<void*>(s_yardEnt), before.natural, before.claimed, before.markers, after.markers);
    std::fflush(stderr);
}

}  // namespace

extern "C" void ClearOverlap_Tick(PlayerToolManager* ui, int frameNum) {
    Init();
    if (s_start < 0 || !ui || !GameUpdateLoop::_singleton) return;
    if (frameNum < s_start) return;
    if (!s_placed) {
        s_placed = true;
        PlaceHallAndClear(ui, frameNum);
        Census(frameNum);
        return;
    }
    if (s_area.w == 0) return;
    if (!s_stockpilePlaced && frameNum >= s_start + s_delay) {
        Census(frameNum);
        PlaceStockpile(ui, frameNum);
        Census(frameNum);
        return;
    }
    if ((frameNum - s_start) % s_period == 0) Census(frameNum);
}
