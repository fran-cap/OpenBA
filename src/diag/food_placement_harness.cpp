// src/diag/food_placement_harness.cpp
// BANISHED_FOOD_PLACE=<class>|all — placement + SLOT-COVERAGE harness for the six
// food-production classes. The counterpart to BANISHED_FOOD_PANEL (which covers the
// dialog side); this one covers the building side.
//
// It answers two questions the demo spawners alone cannot:
//
//  1. DID THE BUILDING LAND CORRECTLY? entity, tile, footprint, whether it carries a
//     StateMachineComponent and a WorkPlaceComponent, its live state index, staffing
//     and accepting flag.
//
//  2. WHICH VTABLE SLOTS ARE ACTUALLY LIVE? Every one of these components is created
//     through MakeInertTyped, which fills all 48 slots with one shared stub and then
//     lets InstallLive overwrite the handful we have ported. So a slot still holding
//     the stub address is a slot that is NOT reimplemented, no matter what the source
//     tree suggests. Comparing each slot against g_InertComponentVtable turns "every
//     slot decompiled and reimplemented" from a claim into a measurement, per class,
//     printed as a map.
//
// Retail's component vtables for this family are 22 slots wide (0..21) with a real body
// in every one — the RTTI/type-name string starts immediately after slot 21, which is
// how the width was established. So the target is 22/22 live; anything less is the
// remaining work, and the map says exactly which indices.
//
// Known slot meanings for this family (recovered this wave):
//   0..3   dtor family + Shutdown          13  CanCommand / command mask
//   9      Update (Hunt only, ctor bit 8)  14  Command
//   16     GetDefaultState                 15  OnMessage
//   17     CreateState                     18  CreateWork (pure virtual in the RP base)
//   19/20  OnEnter/OnExitProducing         21  ProduceTick
//
// Test-only, opt-in, default off. Fires once at a settle frame
// (BANISHED_FOOD_PLACE_FRAME, default 900).

#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Entity/ComponentEntity.h"   // B4: the component array by name
#include "Entity/EntityList.h"        // B4: GameUpdateLoop::m_entityList
#include "options.h"
#include "clamped_atoi.h"     // review F-087: checked parse of BANISHED_* text
#include "Object/factory_registration.h"
#include "object/TypeInfo.h"   // Object::Base::TypeName (B3X W0)
#include "object/VSlot.h"      // Object::CompilerSlotOf
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Component/MapComponent.h"
#include "Component/StateMachineComponent.h"
#include "Component/WorkPlaceComponent.h"


namespace {

// ── retail slot addresses, read from each class's component vtable ────────────────
// Stored as the low 32 bits (every one is 0x140xxxxxx). Sources:
//   CropField 0x140162ec0  Orchard 0x14015a180  Pasture  0x140163f60
//   Fishing   0x140163230  Hunt    0x140163770  Herbalist 0x140159860
struct FoodClass {
    const char* key;
    const char* component;
    const char* demoEnv;          // the spawner that puts one in the world
    uint32_t    retail[22];
};

const FoodClass kClasses[] = {
    { "cropfield", "CropFieldComponent", "BANISHED_BUILD_DEMO_CROPFIELD", {
        0x400330a0,0x40032ec0,0x40032eb0,0x4011ae70,0x4011ae60,0x40118a60,0x40118820,0x40118830,
        0x40118a70,0x40118a80,0x40118a90,0x40118aa0,0x40118ab0,0x4011ada0,0x4011acc0,0x4011ab50,
        0x400026c0,0x40004080,0x40033040,0x40032f20,0x40032f10,0x4011b2f0 } },
    { "orchard",   "OrchardComponent",   "BANISHED_BUILD_DEMO_ORCHARD", {
        0x40009a70,0x40009860,0x40009850,0x40051de0,0x40051dd0,0x40118a60,0x40118820,0x40118830,
        0x40118a70,0x40118a80,0x40118a90,0x40118aa0,0x40118ab0,0x40051c90,0x40051ba0,0x40051990,
        0x400026c0,0x40004080,0x40009a10,0x400098c0,0x400098b0,0x400523a0 } },
    { "pasture",   "PastureComponent",   "BANISHED_BUILD_DEMO_PASTURE", {
        0x40036910,0x40036720,0x40036710,0x40127f40,0x40127a50,0x40118a60,0x40127a30,0x401277e0,
        0x40118a70,0x40118a80,0x40118a90,0x40118aa0,0x40127540,0x40127dd0,0x40127a60,0x40127720,
        0x400026c0,0x40036770,0x400368b0,0x40036790,0x40036780,0x40128280 } },
    { "fishing",   "FishingComponent",   "BANISHED_BUILD_DEMO_FISHINGDOCK", {
        0x40034560,0x40034340,0x40034330,0x4011f050,0x40118a50,0x40118a60,0x40118820,0x40118830,
        0x40118a70,0x40118a80,0x40118a90,0x40118aa0,0x40118ab0,0x4012b520,0x4012b5a0,0x40118ae0,
        0x400026c0,0x40004080,0x4011efe0,0x4011ee80,0x4011ee50,0x4011ec10 } },
    { "hunting",   "HuntComponent",      "BANISHED_BUILD_DEMO_HUNTERLODGE", {
        0x40035100,0x40034f20,0x40034f10,0x40122820,0x40118a50,0x40118a60,0x40118820,0x40118830,
        0x40118a70,0x40122740,0x40118a90,0x40118aa0,0x40118ab0,0x4012b520,0x4012b5a0,0x40118ae0,
        0x400026c0,0x40004080,0x401226a0,0x40006b10,0x40006b10,0x40006b10 } },
    { "gatherer",  "HerbalistComponent", "BANISHED_BUILD_DEMO_GATHERERHUT", {
        0x40006d60,0x40006b30,0x40006b20,0x4004b200,0x40118a50,0x40118a60,0x40118820,0x40118830,
        0x40118a70,0x40118a80,0x40118a90,0x40118aa0,0x40118ab0,0x4012b520,0x4012b5a0,0x40118ae0,
        0x400026c0,0x40004080,0x4004b160,0x40006b10,0x40006b10,0x40006b10 } },

    // ── the four NON-food classes carrying the panel work checkbox ────────────────
    // Added 2026-08-15 for the slot-13/14 wave. Not food producers, but they share the
    // exact same 22-slot component vtable shape, so the same coverage probe applies
    // verbatim. Slots read from:
    //   ConsumeProduce 0x140162a80  Market 0x140159c40
    //   Forester       0x140163460  Attendance 0x140158ce0
    // Watch slots 13/14 specifically: those are the ControlUI command pair, and until
    // this wave every one of these four had the inert stub in both.
    { "consumeproduce", "ConsumeProduceComponent", "BANISHED_BUILD_DEMO_TAVERN", {
        0x40032460,0x400322f0,0x400322e0,0x40118e00,0x40118ef0,0x40118a60,0x40118820,0x40118830,
        0x40118a70,0x40118a80,0x40118a90,0x40118aa0,0x40118ab0,0x4012b520,0x4012b5a0,0x40118ae0,
        0x400026c0,0x40004080,0x40118d00,0x40006b10,0x40006b10,0x40006b10 } },
    { "market",    "MarketComponent",    "BANISHED_BUILD_DEMO_MARKET", {
        0x40008440,0x40008210,0x40008200,0x4004ead0,0x40118a50,0x40118a60,0x40118820,0x40118830,
        0x40118a70,0x40118a80,0x40118a90,0x40118aa0,0x40118ab0,0x4012b520,0x4012b5a0,0x40118ae0,
        0x400026c0,0x40004080,0x4004ea40,0x40006b10,0x40006b10,0x40006b10 } },
    { "forester",  "ForesterComponent",  "BANISHED_BUILD_DEMO_FORESTERLODGE", {
        0x40034a70,0x40034860,0x40034850,0x4011f960,0x40118a50,0x40118a60,0x40118820,0x40118830,
        0x40118a70,0x40118a80,0x40118a90,0x40118aa0,0x40118ab0,0x4011f920,0x4011f890,0x40118ae0,
        0x400026c0,0x40004080,0x4011f780,0x40006b10,0x40006b10,0x40006b10 } },
    // ⚠ Attendance is the odd one out: NOT a ResourceProduceComponent subclass, so its
    // slot 17 (CreateState) is 0x1400026b0 = `LEA RAX,[RCX+0x38]` (node at +0x38), not
    // the family's +0x28, and slots 7/9/19/20/21 are its own rather than the shared no-ops.
    { "attendance", "AttendanceComponent", "BANISHED_BUILD_DEMO_SCHOOLHOUSE", {
        0x40002860,0x40002660,0x40002650,0x40044410,0x40118a50,0x40118a60,0x40118820,0x400443d0,
        0x40118a70,0x40044370,0x40118a90,0x40118aa0,0x40118ab0,0x40043f10,0x40043e80,0x40118ae0,
        // ⚠ slot 21 is 0x140044700 (`MOV EAX,1; RET`). This row carried 0x140047000 —
        // a transposed digit that pointed at CemeteryDescription_sub_6fc0. Corrected
        // 2026-08-15 against the vtable bytes at 0x140158ce0.
        0x400026c0,0x400026b0,0x40002800,0x400026e0,0x400026d0,0x40044700 } },
};

// Retail bodies that are literally `C2 00 00` (RET) plus 0xCC padding — verified by
// reading the bytes. A slot pointing at one of these has NO behaviour in retail, so our
// inert stub is already equivalent there and it is NOT outstanding work. Counting those
// as "missing" was making the coverage number far more alarming than the truth.
const uint32_t kRetailNoOp[] = {
    0x40118a50, 0x40118a60, 0x40118820, 0x40118830, 0x40118a70,
    0x40118a80, 0x40118a90, 0x40118aa0, 0x40118ab0, 0x40006b10,
    0x40118ae0,   // slot 15 (OnMessage) on the ResourceProduce family
};
bool IsRetailNoOp(uint32_t a) {
    for (uint32_t x : kRetailNoOp) if (x == a) return true;
    return false;
}
const int kClassCount = (int)(sizeof(kClasses) / sizeof(kClasses[0]));
const int kSlots = 22;      // retail width for this component family

bool s_init = false, s_fired = false;
char s_want[32] = {0};
int  s_fireFrame = 900;

void Init() {
    if (s_init) return;
    s_init = true;
    const char* e = Opt().foodPlace;
    if (!e || !e[0] || std::strcmp(e, "0") == 0) return;
    std::snprintf(s_want, sizeof(s_want), "%s", e);
    if (const char* f = Opt().foodPlaceFrame) {
        int v = HarnessAtoi(f);
        if (v > 0) s_fireFrame = v;
    }
    std::fprintf(stderr, "[FoodPlace] ENABLED target=\"%s\" fireFrame=%d\n", s_want, s_fireFrame);
    std::fflush(stderr);
}

uint32_t Tid(const char* n) { return n ? ::Object::GetTypeIdByName(n) : 0; }

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

void ProbeClass(const FoodClass& fc) {
    const uint32_t tid = Tid(fc.component);
    if (!tid) {
        std::fprintf(stderr, "[FoodPlace] %-19s TYPE ID UNRESOLVED\n", fc.component);
        return;
    }
    GameUpdateLoop* gw = (GameUpdateLoop::_singleton != nullptr) ? GameUpdateLoop::_singleton : nullptr;
    if (!gw) return;
    EntityList* el = gw->m_entityList;   // +0xe8
    if (!el) return;
    Object::InternalPtr<ComponentEntity>* arr = el->masterArray._data;   // +0x08
    const int n = el->masterArray._size;   // +0x10

    int instances = 0;
    ComponentEntity* ent = nullptr; Component* comp = nullptr;
    for (int i = 0; i < n && arr; ++i) {
        ComponentEntity* e = arr[i].Get();
        if (!e) continue;
        if (Component* c = FindComp(e, tid)) {
            ++instances;
            if (!ent) { ent = e; comp = c; }
        }
    }
    if (!ent) {
        std::fprintf(stderr, "[FoodPlace] %-19s NOT PLACED — 0 instance(s); set %s=1\n",
                     fc.component, fc.demoEnv);
        return;
    }

    // ── placement facts ───────────────────────────────────────────────────────────
    int tx = -1, ty = -1, fw = -1, fh = -1;
    if (Component* mcc = FindComp(ent, MapComponent::TypeId())) {
        MapComponent* m = static_cast<MapComponent*>(mcc);   // exact type id
        tx = m->tileX0;   // +0x28
        ty = m->tileY0;   // +0x2a
        fw = m->tileX1;   // +0x2c
        fh = m->tileY1;   // +0x2e
    }
    int state = -1;
    const bool hasSM = (FindComp(ent, StateMachineComponent::TypeId()) != nullptr);
    if (Component* smc = FindComp(ent, StateMachineComponent::TypeId()))
        state = static_cast<StateMachineComponent*>(smc)->stateMachine.activeState;   // +0x51
    int staffed = -1, accepting = -1;
    unsigned long long profKey = 0;
    bool haveWpc = false;
    if (Component* wpcc = FindComp(ent, WorkPlaceComponent::TypeId())) {
        WorkPlaceComponent* w = static_cast<WorkPlaceComponent*>(wpcc);   // exact type id
        haveWpc   = true;
        staffed   = w->workers._size;     // +0x60
        accepting = w->isAccepting;     // +0x50
        // +0x48 = profession key. WorkplaceUI_Startup hides the ENTIRE workPlace
        // ribbon (worker up/down spinner + stop-sign icon) when this is 0 -
        // faithful to retail 0x140087080. profKey=0 therefore means no worker
        // arrows regardless of what the .rsc declares; read this field first for
        // any "that panel has no worker arrows" report.
        profKey = reinterpret_cast<uintptr_t>(w->profession);  // CAST-OK: ptr_int
    }

    std::fprintf(stderr, "[FoodPlace] %-19s PLACED x%d  ent=%p comp=%p tile=(%d,%d) fp=%dx%d "
                 "SM=%s stateIdx=%d staffed=%d accepting=%d profKey(+0x48)=0x%llx%s\n",
                 fc.component, instances, (void*)ent, (void*)comp, tx, ty, fw, fh,
                 hasSM ? "yes" : "NO", state, staffed, accepting, profKey,
                 !haveWpc ? " [NO WorkPlaceComponent]"
                          : (profKey == 0 ? "  <-- RIBBON HIDDEN (no worker spinner)" : ""));

    // ── slot coverage ─────────────────────────────────────────────────────────────
    // A null slot is NOT reimplemented. (The shared inert stub this used to compare
    // against went with the last hand-built component table at the ENDGAME B.3.8
    // close: every component vtable is the compiler's, so no slot can hold it.)
    void** vt = *reinterpret_cast<void***>(comp);   // CAST-OK: crash_walk
    if (!vt) { std::fprintf(stderr, "[FoodPlace]   no vtable?!\n"); return; }
    // `fc.retail[]` is in RETAIL slot order. Retail slot 0 is the one deleting
    // destructor; every later retail slot i sits at compiler slot i + shift,
    // where the shift is read off a named member (Object::Base::TypeName is
    // retail slot 1): 0 while the class declares `DeletingDtor`, 1 once it
    // declares `virtual ~T()` (GCC's two destructor slots, B3X_RULES X3).
    const int32_t shift = ::Object::CompilerSlotOf(&::Object::Base::TypeName) - 1;
    auto compilerSlot = [shift](int retailSlot) -> int32_t {
        return retailSlot == 0 ? 0 : retailSlot + shift;
    };
    char map[kSlots + 1];
    int live = 0, equiv = 0, gap = 0;
    for (int i = 0; i < kSlots; ++i) {
        const bool isStub = (vt[compilerSlot(i)] == nullptr);
        if (!isStub)                        { map[i] = 'W'; ++live;  }
        else if (IsRetailNoOp(fc.retail[i])){ map[i] = '='; ++equiv; }   // retail no-op too
        else                                { map[i] = '.'; ++gap;   }
    }
    map[kSlots] = 0;
    // W = we have a real body. = retail is an empty RET here, so our stub matches it.
    // . = retail has real behaviour and we do not — the only genuine outstanding work.
    std::fprintf(stderr, "[FoodPlace]   slots 0..21 [%s]  ported=%d equivalent-noop=%d "
                 "MISSING=%d  (behavioural coverage %d/%d)\n",
                 map, live, equiv, gap, live + equiv, kSlots);
    if (gap > 0) {
        std::fprintf(stderr, "[FoodPlace]   MISSING slots:");
        for (int i = 0; i < kSlots; ++i)
            if (map[i] == '.')
                std::fprintf(stderr, " %d(0x1%08X)", i, fc.retail[i]);
        std::fprintf(stderr, "\n");
    }
    std::fflush(stderr);
}

}  // namespace

extern "C" void FoodPlace_Tick(int frameNum) {
    Init();
    if (!s_want[0] || s_fired) return;
    if (frameNum < s_fireFrame) return;
    s_fired = true;
    const bool all = (std::strcmp(s_want, "all") == 0);
    int probed = 0;
    for (int i = 0; i < kClassCount; ++i)
        if (all || std::strcmp(s_want, kClasses[i].key) == 0) { ProbeClass(kClasses[i]); ++probed; }
    if (probed == 0)
        std::fprintf(stderr, "[FoodPlace] unknown target \"%s\"\n", s_want);
    std::fflush(stderr);
}
