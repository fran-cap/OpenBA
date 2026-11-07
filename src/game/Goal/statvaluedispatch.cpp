// src/game/Goal/statvaluedispatch.cpp
// StatGoal::StatValue (exe 0x1400da4e0) and ALL of its sub-callees.
//
// The dispatcher is a switch(statType) that returns a goal-progress count
// queried from the GameUpdateLoop (citizen counts, resource totals, building
// counts, etc.). Each case calls a small helper at 0x1400d7xxx..0x1400da4xx or
// reads a field inline. Retail passes RCX (the StatGoal) and, for the cases
// that take one, RDX = the resolved External (0x1400da50e `MOV RDX,R8`) to each
// helper unchanged; the helpers that read a StatGoal field (+0x78 statFlags,
// +0x7c statSubType) are StatGoal members, the rest read no field of RCX and
// stay file-local functions (B4_RULES B1). Several helpers call deeper exe
// bodies (the AVL family at 0x140014cxx, 0x140046dd0, 0x140090f20,
// 0x14011e710, the FindComponent<T> instances 0x140004680 / 0x140003ea0);
// those are ported here too, typed.
//
// B.4 TUT: every field is read by name. LEFT (raw): the two CitizenBehavior
// trees (+0x100, +0x160; WORK-a's header declares them `void*`, no node type)
// and WorldGeometry's +0x220 int array (GAME-c's header holds it as an untyped
// {data, count|cap} Header).

#include "Goal/StatGoal.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Calendar.h"
#include "Game/Map.h"
#include "Game/ProfessionList.h"
#include "Game/WorldGeometry.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Game/exe_constants.h"   // kRdata_*
#include <cstdint>
#include <cstring>
#include "object/TypeInfo.h"
#include "system/Value.h"      // System::Time
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Component/ComponentDescription.h"
#include "Component/CitizenComponent.h"
#include "Component/CitizenDescription.h"
#include "Component/CemeteryComponent.h"
#include "Component/PastureComponent.h"
#include "Component/RawMaterialDescription.h"
#include "Component/StorageComponent.h"
#include "Component/TrackerComponent.h"
#include "Component/WorkPlaceComponent.h"
#include "Component/WorkPlaceDescription.h"
#include "Component/BuildComponent.h"
#include "Component/DestroyComponent.h"
#include "Component/StateMachineComponent.h"
#include "Component/BuildDescription.h"
#include "Component/MapComponent.h"
#include "Component/TombstoneComponent.h"
#include "Component/SelectDescription.h"
#include "Game/Profession.h"
#include "Game/TerrainGenerator.h"
#include "Game/ClimateGenerator.h"
#include "Component/BuildingState.h"

// Float constants used by 0x1400d8c50 (season-progress weighting): the .rdata
// cells 0x14017225c (3600.0f) and 0x1401721e4 (48.0f), exe_constants.h.
static inline float RdataF(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

// ============================================================
// Small primitives
// ============================================================

// The External as T when it IsDerivedFrom T (null otherwise; a null object
// answers false, Object::IsDerivedFromNullSafe).
template <class T>
static const T* AsType(const Object::Base* obj) {
    return ::Object::IsDerivedFromNullSafe(obj, T::TypeId()) ? static_cast<const T*>(obj) : nullptr;
}

// The first sub-description of `desc` whose Type() is T's, if it IsDerivedFrom
// T: retail's inlined description walk (Type match, then IsDerivedFrom, then
// stop). No null check on `desc`: retail has none.
template <class T>
static T* FindSubDescription(const ComponentDescription* desc) {
    for (int i = 0; i < desc->_subDescCount; i++) {
        Description* sub = desc->_subDescs[i];
        if (sub->Type() == T::TypeId())
            return ::Object::IsDerivedFromNullSafe(sub, T::TypeId()) ? static_cast<T*>(sub) : nullptr;
    }
    return nullptr;
}

// The state byte the state machine reports: the pending state, or the last
// input when nothing is pending (`MOVZX [RCX+0x52]; TEST; JNS; MOVZX [RCX+0x50]`,
// 0x14011e740).
static inline BuildingState ReportedState(const StateMachineComponent* sm) {
    int8_t c = sm->stateMachine.pendingState;
    if (c < 0) c = sm->stateMachine.currentInput;
    return static_cast<BuildingState>(c);
}

// The CitizenBehavior entity-bucket trees: Goal/goal_exe_helpers.h.

// ============================================================
// The dedup set 0x1400d8d70 builds: 0x28-byte AVL nodes keyed by the
// resource's ComponentDescription (FUN_140014c30 / c70 / da0 over this node).
// ============================================================
struct DedupNode {
    const ComponentDescription* key;   // +0x00
    DedupNode* parent;                 // +0x08
    DedupNode* left;                   // +0x10
    DedupNode* right;                  // +0x18
    int16_t    balance;                // +0x20
    int16_t    height;                 // +0x22
    uint8_t    _pad24[4];              // +0x24
};
static_assert(sizeof(DedupNode) == 0x28, "the dedup node is MemAlloc(0x28)");
static_assert(offsetof(DedupNode, parent) == 0x08, "DedupNode +0x08");
static_assert(offsetof(DedupNode, left) == 0x10, "DedupNode +0x10");
static_assert(offsetof(DedupNode, right) == 0x18, "DedupNode +0x18");
static_assert(offsetof(DedupNode, balance) == 0x20, "DedupNode +0x20");
static_assert(offsetof(DedupNode, height) == 0x22, "DedupNode +0x22");

// FUN_140014c30 — recursive post-order free.
static void Avl_Free_14c30(DedupNode* node) {
    if (node) {
        Avl_Free_14c30(node->left);
        Avl_Free_14c30(node->right);
        System::MemoryAllocator::MemFree(node);
    }
}

// Height / balance of one node from its children (a child contributes its
// height + 1, a missing child 0).
static void Avl_Recompute(DedupNode* n) {
    short hl = (n->left == nullptr) ? 0 : (short)(n->left->height + 1);
    short hr = (n->right == nullptr) ? 0 : (short)(n->right->height + 1);
    short h = (hr < hl) ? hl : hr;
    n->balance = (short)(hr - hl);
    n->height = h;
}

// FUN_140014c70 — left rotation + height/balance recompute. Returns the new
// subtree root.
static DedupNode* Avl_RotateLeft_14c70(DedupNode** root, DedupNode* node) {
    if (node == *root) *root = node->right;
    DedupNode* parent = node->parent;
    if (parent != nullptr) {
        if (parent->left == node)
            parent->left = node->right;
        else
            parent->right = node->right;
    }
    DedupNode* r = node->right;
    r->parent = node->parent;
    DedupNode* rl = r->left;
    node->right = rl;
    if (rl != nullptr) rl->parent = node;
    r->left = node;
    node->parent = r;
    if (r->left != nullptr) Avl_Recompute(r->left);
    if (r->right != nullptr) Avl_Recompute(r->right);
    Avl_Recompute(r);
    return r;
}

// FUN_140014da0 — right rotation (mirror of the above).
static DedupNode* Avl_RotateRight_14da0(DedupNode** root, DedupNode* node) {
    if (node == *root) *root = node->left;
    DedupNode* parent = node->parent;
    if (parent != nullptr) {
        if (parent->left == node)
            parent->left = node->left;
        else
            parent->right = node->left;
    }
    DedupNode* l = node->left;
    l->parent = node->parent;
    DedupNode* lr = l->right;
    node->left = lr;
    if (lr != nullptr) lr->parent = node;
    l->right = node;
    node->parent = l;
    if (l->left != nullptr) Avl_Recompute(l->left);
    if (l->right != nullptr) Avl_Recompute(l->right);
    Avl_Recompute(l);
    return l;
}

// Insert key into the AVL set rooted at *root. Returns true if the key was
// newly inserted, false if already present. Faithful to the inline
// insert/retrace loop in 8d70: walk left / right on key compare; on a missing
// child, MemAlloc a 0x28 node and retrace, rotating with c70/da0 when
// |balance| > 1.
static bool Avl_Insert(DedupNode** root, const ComponentDescription* key) {
    if (*root == nullptr) {
        DedupNode* n = static_cast<DedupNode*>(System::MemoryAllocator::MemAlloc(0x28, 8));
        if (n) {
            n->key = key;
            n->parent = nullptr;
            n->left = nullptr;
            n->right = nullptr;
            n->balance = 0;
            n->height = 0;
            *root = n;
        }
        return true;
    }
    DedupNode* cur = *root;
    while (true) {
        if (key < cur->key) {
            if (cur->left == nullptr) break;
            cur = cur->left;
        } else if (key <= cur->key) {
            return false;  // already present
        } else {
            if (cur->right == nullptr) break;
            cur = cur->right;
        }
    }
    bool goLeft = key < cur->key;
    DedupNode* n = static_cast<DedupNode*>(System::MemoryAllocator::MemAlloc(0x28, 8));
    DedupNode* newNode = nullptr;
    if (n) {
        n->key = key;
        n->parent = cur;
        n->left = nullptr;
        n->right = nullptr;
        n->balance = 0;
        n->height = 0;
        newNode = n;
    }
    if (goLeft) cur->left = newNode; else cur->right = newNode;
    // retrace from cur up to the root
    DedupNode* p = cur;
    while (p != nullptr) {
        DedupNode* up = p->parent;
        short hl = (p->left == nullptr) ? 0 : (short)(p->left->height + 1);
        DedupNode* rChild = p->right;
        short hr = (rChild == nullptr) ? 0 : (short)(rChild->height + 1);
        short h = (hr < hl) ? hl : hr;
        p->height = h;
        p->balance = (short)(hr - hl);
        if ((short)(hr - hl) > 1 && rChild != nullptr) {
            if (rChild->balance < 0)
                Avl_RotateRight_14da0(root, rChild);
            Avl_RotateLeft_14c70(root, p);
        }
        if (p->balance < -1) {
            DedupNode* lChild = p->left;
            if (lChild != nullptr) {
                if (lChild->balance > 0)
                    Avl_RotateLeft_14c70(root, lChild);
                Avl_RotateRight_14da0(root, p);
            }
        }
        p = up;
    }
    return true;
}

// ============================================================
// FUN_14011e710 / FUN_140046dd0 / FUN_140090f20 — sub-callees.
// ============================================================

// 0x14011e710 — a DestroyComponent member in retail (COMP's class; kept
// file-local, typed): the owning entity's StateMachineComponent
// (FindComponent<StateMachineComponent>, 0x140003ea0) reports Demolish, HaulAway or Deconstruct
// (states 3, 4 or 5).
static int DestroyComponent_InState345_e710(const DestroyComponent* destroy) {
    StateMachineComponent* sm = destroy->_entity->FindComponent<StateMachineComponent>();
    if (ReportedState(sm) != BuildingState::Demolish) {
        if (ReportedState(sm) != BuildingState::HaulAway) {
            if (ReportedState(sm) != BuildingState::Deconstruct) return 0;
        }
    }
    return 1;
}

// 0x140046dd0 — a CemeteryComponent member in retail (COMP-a's class; kept
// file-local, typed; the same body as Dialog/cemeteryui.cpp's
// CemeteryGetBurialCount_Impl): the plots whose map-cell occupant has a
// TombstoneComponent.
static int Cemetery_CountBurials_6dd0(const CemeteryComponent* cemetery) {
    int matched = 0;
    Map* map = GameUpdateLoop::_singleton->m_game;
    int slotCount = cemetery->plots._size;
    if (slotCount > 0) {
        for (int i = 0; i < slotCount; i++) {
            ComponentEntity* occupant = map->cells.GetData()[cemetery->plots[i]].occupant.Get();
            if (occupant != nullptr) {
                if (occupant->FindComponent<TombstoneComponent>() != nullptr) matched++;
            }
        }
    }
    return matched;
}

// 0x140090f20 — a CitizenBehavior member in retail (WORK-a's class; kept
// file-local, typed): sum the int series `series` of the +0x150 table over the
// [first..last] index window, two columns at a time.
static int CitizenBehavior_SumSeries_0f20(const CitizenBehavior* cb, int series, int first, int last) {
    const Collection::Array<int32_t>& row = cb->field_0x150._data[series];
    if (first < 0) first = 0;
    int rowCount = row.GetSize();
    if (rowCount <= last) last = rowCount - 1;
    int64_t lo = first, hi = last;
    int colA = 0, colB = 0, tail = 0;
    if (lo <= hi) {
        if ((hi - lo) + 1 > 1) {
            const int* p = row.GetData() + lo;
            int64_t pairs = ((hi - lo) - 1) / 2 + 1;
            lo = lo + pairs * 2;
            do {
                colA += p[0];
                colB += p[1];
                p += 2;
                pairs--;
            } while (pairs != 0);
        }
        if (lo <= hi) tail = row.GetData()[lo];
        return colB + colA + tail;
    }
    return 0;
}

// ============================================================
// Dispatcher helpers (0x1400d7cf0 .. 0x1400da410)
// ============================================================

static inline CitizenBehavior* Citizens() { return GameUpdateLoop::_singleton->m_citizenList; }

// A citizen is an adult (age >= its description's adult age) and not at school.
static inline bool IsAdultNotInSchool(const CitizenComponent* c) {
    return c->description->adultAge <= c->age && c->school == nullptr;
}

// 0x1400da410 — count "adult employable" citizens.
static uint32_t Helper_a410() {
    CitizenBehavior* cb = Citizens();
    int n = cb->citizens._size;
    int count = 0;
    for (int i = 0; i < n; i++) {
        const CitizenComponent* c = cb->citizens._data[i].Get();
        if (IsAdultNotInSchool(c)) {
            int minAge = c->description->maxEducation;
            if (minAge <= c->education) count++;
        }
    }
    return (uint32_t)count;
}

// 0x1400da2a0 — count adult citizens whose +0xA8 matches the resource's
// RawMaterialDescription +0x54 (or whose +0xA8 is < 0). ext is a
// ComponentDescription.
static uint32_t Helper_a2a0(const Object::External* ext) {
    CitizenBehavior* cb = Citizens();
    int n = cb->citizens._size;
    // The narrowed container and its RawMaterialDescription are read with no test:
    // `*(int*)(container + 0x30)` and, per adult, `*(int*)(prof + 0x54)`.
    const ComponentDescription* container = AsType<ComponentDescription>(ext);
    RawMaterialDescription* prof = FindSubDescription<RawMaterialDescription>(container);
    int count = 0;
    for (int i = 0; i < n; i++) {
        const CitizenComponent* c = cb->citizens._data[i].Get();
        if (IsAdultNotInSchool(c)) {
            int homeId = c->field_0xA8;
            int wantId = prof->_i54;
            if (homeId == wantId) { count++; continue; }
            if (c->field_0xA8 < 0) count++;
        }
    }
    return (uint32_t)count;
}

// 0x1400da150 — count citizens by +0x1E0 state vs a flag bit (_flags & 0x8000).
static uint32_t Helper_a150(const Object::External* ext) {
    CitizenBehavior* cb = Citizens();
    int n = cb->citizens._size;
    // No test of the container or the RawMaterialDescription: `(*(uint*)(prof + 0x5c)
    // & 0x8000)` is read per citizen, for the states that test it.
    const ComponentDescription* container = AsType<ComponentDescription>(ext);
    RawMaterialDescription* prof = FindSubDescription<RawMaterialDescription>(container);
    int count = 0;
    for (int i = 0; i < n; i++) {
        const CitizenComponent* c = cb->citizens._data[i].Get();
        int state = c->field_0x1E0;
        if ((((state - 2U) & 0xfffffffd) == 0 && (prof->_flags & 0x8000) != 0) ||
            ((state == 1 || state == 4) && (prof->_flags & 0x8000) == 0)) {
            count++;
        }
    }
    return (uint32_t)count;
}

// 0x1400da0a0 — average citizen happiness (rounded toward zero).
static uint32_t Helper_a0a0() {
    CitizenBehavior* cb = Citizens();
    int n = cb->citizens._size;
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        sum += cb->citizens._data[i]->happiness;
    }
    return (n == 0) ? 0u : (uint32_t)(int)(sum / (float)n);
}

// 0x1400d9ff0 — average citizen health.
static uint32_t Helper_9ff0() {
    CitizenBehavior* cb = Citizens();
    int n = cb->citizens._size;
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        sum += cb->citizens._data[i]->health;
    }
    return (n == 0) ? 0u : (uint32_t)(int)(sum / (float)n);
}

// 0x1400d9f20 — count adult citizens (no education test, no house test).
static uint32_t Helper_9f20() {
    CitizenBehavior* cb = Citizens();
    int n = cb->citizens._size;
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (IsAdultNotInSchool(cb->citizens._data[i].Get())) count++;
    }
    return (uint32_t)count;
}

// 0x1400d9e80 — count citizens at school.
static uint32_t Helper_9e80() {
    CitizenBehavior* cb = Citizens();
    int n = cb->citizens._size;
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (cb->citizens._data[i]->school != nullptr) count++;
    }
    return (uint32_t)count;
}

// 0x1400d9dc0 — count citizens that are (under adult age OR at school) AND not
// at school. (Faithful to the exact two-predicate form.)
static uint32_t Helper_9dc0() {
    CitizenBehavior* cb = Citizens();
    int n = cb->citizens._size;
    int count = 0;
    for (int i = 0; i < n; i++) {
        const CitizenComponent* c = cb->citizens._data[i].Get();
        if ((c->age < c->description->adultAge || c->school != nullptr)) {
            if (c->school == nullptr) count++;
        }
    }
    return (uint32_t)count;
}

// 0x1400d9cd0 — count adult citizens whose profession == ext (a Profession).
static uint32_t Helper_9cd0(const Object::External* ext) {
    CitizenBehavior* cb = Citizens();
    int n = cb->citizens._size;
    const Profession* prof = AsType<Profession>(ext);
    int count = 0;
    for (int i = 0; i < n; i++) {
        const CitizenComponent* c = cb->citizens._data[i].Get();
        if (IsAdultNotInSchool(c)) {
            if (c->profession == prof) count++;
        }
    }
    return (uint32_t)count;
}

// 0x1400d9c80 — true if the loop's TerrainGenerator is ext.
static uint32_t Helper_9c80(const Object::External* ext) {
    const TerrainGenerator* p = GameUpdateLoop::_singleton->m_terrainConfig;
    const TerrainGenerator* q = AsType<TerrainGenerator>(ext);
    return (p == q) ? 1u : 0u;
}

// 0x1400d9c10 — true if the loop's ClimateGenerator is ext.
static uint32_t Helper_9c10(const Object::External* ext) {
    const ClimateGenerator* p = GameUpdateLoop::_singleton->m_climateConfig;
    const ClimateGenerator* q = AsType<ClimateGenerator>(ext);
    return (p == q) ? 1u : 0u;
}

// 0x1400d9b80 — burials summed over every cemetery.
static uint32_t Helper_9b80() {
    CitizenBehavior* cb = Citizens();
    int n = cb->cemeteries._size;
    int total = 0;
    for (int i = 0; i < n; i++) {
        total += Cemetery_CountBurials_6dd0(cb->cemeteries._data[i].Get());
    }
    return (uint32_t)total;
}

// Generic body for 0x1400d9560 / 9470 / 9380: scan the +0x120 description
// list; count those whose SelectDescription +0x40 == `wantState`.
static int LimitedResourceCount(uint32_t wantState) {
    CitizenBehavior* cb = Citizens();
    int n = cb->field_0x120._size;
    int count = 0;
    for (int i = 0; i < n; i++) {
        SelectDescription* comp = FindSubDescription<SelectDescription>(cb->field_0x120._data[i]);
        if (comp && comp->_u40 == wantState) count++;
    }
    return count;
}

// 0x1400d9560 — state == 2.
static uint32_t Helper_9560() { return (uint32_t)LimitedResourceCount(2); }
// 0x1400d9470 — state == 0.
static uint32_t Helper_9470() { return (uint32_t)LimitedResourceCount(0); }
// 0x1400d9380 — state == 1.
static uint32_t Helper_9380() { return (uint32_t)LimitedResourceCount(1); }

// 0x1400d92d0 — the animals of every pasture that keeps ext.
static uint32_t Helper_92d0(const Object::External* ext) {
    CitizenBehavior* cb = Citizens();
    int n = cb->pastures._size;
    int total = 0;
    for (int i = 0; i < n; i++) {
        const PastureComponent* e = cb->pastures._data[i].Get();
        if (e->livestockType == ext) {
            total += e->animals._size;
        }
    }
    return (uint32_t)total;
}

// 0x1400d8d70 — count distinct resources (AVL-deduped) held in the trackers of
// the profession buckets' entities whose RawMaterialDescription flags
// intersect statFlags, optionally filtered by template == ext.
uint32_t StatGoal::CountDistinctTracked(const Object::External* ext) const {
    CitizenBehavior* cb = Citizens();
    DedupNode* dedupRoot = nullptr;   // AVL set "local_60"
    int distinct = 0;                 // "local_58"

    ProfessionList* professions = GameUpdateLoop::_singleton->m_professionList;
    int regionCount = professions->professions.GetSize();
    for (int ri = 0; ri < regionCount; ri++) {
        Profession* regionKey = GameUpdateLoop::_singleton->m_professionList->professions.GetData()[ri];

        // BST lookup at +0x100 keyed by regionKey -> bucket count.
        int bucketCount = 0;
        {
            ProfessionBucket* node = cb->field_0x100._root;
            while (node) {
                if (regionKey < node->element.key) node = node->left;
                else if (regionKey <= node->element.key) { bucketCount = node->element.entities._size; break; }
                else node = node->right;
            }
        }
        for (int bi = 0; bi < bucketCount; bi++) {
            // re-lookup node, then bucket entry [bi].
            ProfessionBucket* node = cb->field_0x100._root;
            ProfessionBucket* match = nullptr;
            while (node) {
                if (regionKey < node->element.key) node = node->left;
                else { match = node; if (regionKey <= node->element.key) break; node = node->right; }
            }
            if (!match) continue;
            ComponentEntity* entityRef = match->element.entities._data[bi].Get();
            TrackerComponent* tracker = entityRef->FindComponent<TrackerComponent>();
            if (tracker == nullptr) continue;
            // optional owner filter: the entity's template == ext.
            if (ext != nullptr) {
                if (entityRef->_description != ext) continue;
            }
            int childN = tracker->displayed._size;
            for (int ci = 0; ci < childN; ci++) {
                const ComponentDescription* childObj = tracker->displayed._data[ci]._resource;
                RawMaterialDescription* prof = FindSubDescription<RawMaterialDescription>(childObj);
                uint32_t selfFlags = (uint32_t)statFlags;
                uint32_t profFlags = prof->_flags;   // +0x5c, no test
                if ((selfFlags & profFlags) != 0) {
                    if (Avl_Insert(&dedupRoot, childObj)) distinct++;
                }
            }
        }
    }
    if (dedupRoot != nullptr) {
        Avl_Free_14c30(dedupRoot->left);
        Avl_Free_14c30(dedupRoot->right);
        System::MemoryAllocator::MemFree(dedupRoot);
    }
    return (uint32_t)distinct;
}

// 0x1400d8c50 — season-weighted production lookup. Picks a series from the
// statFlags bits, then sums the window via 0x140090f20.
uint32_t StatGoal::SumProductionWindow() const {
    uint32_t flags = (uint32_t)statFlags;
    CitizenBehavior* cb = Citizens();
    int col;
    if ((flags >> 0x11) & 1)       col = 0x22;
    else if ((flags >> 0xe) & 1)   col = 0x1f;
    else if (flags & 0x40)         col = 0x13;
    else if (flags & 0x20)         col = 0x19;
    else if ((flags >> 0xd) & 1)   col = 0x1c;
    else if ((flags >> 0xc) & 1)   col = 0x10;
    else if ((flags >> 0xb) & 1)   col = 0xd;
    else if ((flags >> 0xa) & 1)   col = 0xa;
    else if ((flags >> 0x9) & 1)   col = 0x16;
    else                           return 0;

    Calendar* clock = GameUpdateLoop::_singleton->m_dayCounter;
    int dayOfYear = clock->year;
    float secs = clock->yearTime.AsSeconds();
    float div = RdataF(kRdata_14017225c);   // 0x14017225c
    float mul = RdataF(kRdata_1401721e4);   // 0x1401721e4
    int frac = (int)((secs / div) * mul);
    if (frac >= 0x30) frac = 0;
    // NOT RETAIL (review F-165): the window ends wrap as retail's 32-bit arithmetic
    // does instead of overflowing int (StatGoal.h).
    int lo = ProductionWindowLo(clock->year, statSubType);
    int hi = ProductionWindowHi(dayOfYear, frac);
    return (uint32_t)CitizenBehavior_SumSeries_0f20(cb, col, lo, hi);
}

// 0x1400d88e0 — sum the tracked quantities whose RawMaterialDescription flags
// intersect statFlags, over the entities of ext's workplace profession bucket.
uint32_t StatGoal::SumTrackedForProfession(const Object::External* ext) const {
    CitizenBehavior* cb = Citizens();
    WorkPlaceDescription* stockComp = nullptr;
    {   // FindSubDescription<WorkPlaceDescription> on ext
        const ComponentDescription* container = AsType<ComponentDescription>(ext);
        stockComp = FindSubDescription<WorkPlaceDescription>(container);   // no test of the container
    }
    Profession* key = stockComp->_profession0;

    // BST at +0x100 keyed by `key` -> bucket count.
    int bucketCount = 0;
    {
        ProfessionBucket* node = cb->field_0x100._root;
        while (node) {
            if (key < node->element.key) node = node->left;
            else if (key <= node->element.key) { bucketCount = node->element.entities._size; break; }
            else node = node->right;
        }
    }
    int total = 0;
    for (int bi = 0; bi < bucketCount; bi++) {
        ProfessionBucket* node = cb->field_0x100._root;
        while (node) {
            if (key < node->element.key) node = node->left;
            else { if (key <= node->element.key) break; node = node->right; }
        }
        ComponentEntity* entityRef = node->element.entities._data[bi].Get();   // no test of the node
        if (ext != nullptr) {
            if (entityRef->_description != ext) continue;
        }
        TrackerComponent* tracker = entityRef->FindComponent<TrackerComponent>();
        if (tracker == nullptr) continue;
        int childN = tracker->displayed._size;
        for (int ci = 0; ci < childN; ci++) {
            const ComponentDescription* childObj = tracker->displayed._data[ci]._resource;
            RawMaterialDescription* prof = FindSubDescription<RawMaterialDescription>(childObj);
            uint32_t selfFlags = (uint32_t)statFlags;
            uint32_t profFlags = prof->_flags;   // +0x5c, no test
            if ((selfFlags & profFlags) != 0) {
                total += tracker->displayed._data[ci]._count;
            }
        }
    }
    return (uint32_t)total;
}

// 0x1400d86e0 — sum the workers of the WorkPlaceComponent over the entities in
// the bucket keyed by ext's workplace profession.
static uint32_t Helper_86e0(const Object::External* ext) {
    CitizenBehavior* cb = Citizens();
    WorkPlaceDescription* stockComp = nullptr;
    {
        const ComponentDescription* container = AsType<ComponentDescription>(ext);
        stockComp = FindSubDescription<WorkPlaceDescription>(container);   // no test of the container
    }
    Profession* key = stockComp->_profession0;

    int bucketCount = 0;
    {
        ProfessionBucket* node = cb->field_0x100._root;
        while (node) {
            if (key < node->element.key) node = node->left;
            else if (key <= node->element.key) { bucketCount = node->element.entities._size; break; }
            else node = node->right;
        }
    }
    int total = 0;
    for (int bi = 0; bi < bucketCount; bi++) {
        ProfessionBucket* node = cb->field_0x100._root;
        while (node) {
            if (key < node->element.key) node = node->left;
            else { if (key <= node->element.key) break; node = node->right; }
        }
        ComponentEntity* entityRef = node->element.entities._data[bi].Get();   // no test of the node
        WorkPlaceComponent* comp = entityRef->FindComponent<WorkPlaceComponent>();
        total += comp->workers._size;
    }
    return (uint32_t)total;
}

// 0x1400d86a0 — look statSubType up in WorldGeometry's +0x220 int array;
// return the entry or 0.
uint32_t StatGoal::ReadSubTypeEntry() const {
    int idx = statSubType;
    WorldGeometry* pc = GameUpdateLoop::_singleton->m_worldGeometry;
    const Collection::Array<int32_t>& ints = pc->_batchCounters;   // +0x220
    if (idx >= 0 && idx < ints._size) {
        return static_cast<uint32_t>(ints._data[idx]);
    }
    return 0;
}

// 0x1400d85f0 — count the +0x20 list's entities whose template == ext. If ext
// is null, returns the raw count.
static uint32_t Helper_85f0(const Object::External* ext) {
    CitizenBehavior* cb = Citizens();
    int n = cb->field_0x20._size;
    if (ext == nullptr) return (uint32_t)n;
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (cb->field_0x20._data[i]->_description == ext) count++;
    }
    return (uint32_t)count;
}

// The common head of 0x1400d81b0 and 0x1400d7cf0: the bucket entity is
// finished (no BuildComponent, or the description's cost times costMultiplier
// <= laborProgress) and its DestroyComponent's entity reports neither Collapse
// (6) nor Rubble (7) (then again not Collapse). Returns the DestroyComponent, or null to skip.
static DestroyComponent* FinishedStanding(ComponentEntity* entRef) {
    BuildComponent* work = entRef->FindComponent<BuildComponent>();
    DestroyComponent* destroy = entRef->FindComponent<DestroyComponent>();

    bool done;
    if (work == nullptr) {
        done = true;
    } else {
        const BuildDescription* desc = AsType<BuildDescription>(work->_description);
        int needed = desc->_i30 * work->costMultiplier;   // no test of the BuildDescription narrow
        done = (needed <= work->laborProgress);
    }
    if (!done) return nullptr;

    // the destroy component's entity: skip Collapse (6) or Rubble (7).
    StateMachineComponent* cat = destroy->_entity->FindComponent<StateMachineComponent>();
    if (ReportedState(cat) == BuildingState::Collapse) return nullptr;
    if (ReportedState(cat) == BuildingState::Rubble) return nullptr;

    // second state fetch (local_50) — skip if Collapse.
    StateMachineComponent* cat2 = destroy->_entity->FindComponent<StateMachineComponent>();
    if (ReportedState(cat2) == BuildingState::Collapse) return nullptr;
    return destroy;
}

// 0x1400d81b0 — count finished, standing structures of template ext, by
// walking the +0x160 tree keyed by ext.
static uint32_t Helper_81b0(const Object::External* ext) {
    CitizenBehavior* cb = Citizens();
    const ComponentDescription* keyObj = AsType<ComponentDescription>(ext);

    // BST at +0x160 keyed by keyObj -> bucket count.
    int bucketCount = 0;
    {
        TemplateBucket* node = cb->field_0x160._root;
        while (node) {
            if (keyObj < node->element.key) node = node->left;
            else if (keyObj <= node->element.key) { bucketCount = node->element.entities._size; break; }
            else node = node->right;
        }
    }
    int result = 0;
    for (int bi = 0; bi < bucketCount; bi++) {
        TemplateBucket* node = cb->field_0x160._root;
        ComponentEntity* entRef = nullptr;
        while (node) {
            if (keyObj < node->element.key) node = node->left;
            else if (keyObj <= node->element.key) { entRef = node->element.entities._data[bi].Get(); break; }
            else node = node->right;
        }

        DestroyComponent* destroy = FinishedStanding(entRef);
        if (destroy == nullptr) continue;

        if (DestroyComponent_InState345_e710(destroy) == 0) result++;
    }
    return (uint32_t)result;
}

// 0x1400d7cf0 — like 81b0 but on the done-path computes the max footprint
// extent over the MapComponent (FindComponent<MapComponent> 0x140004680).
static uint32_t Helper_7cf0(const Object::External* ext) {
    CitizenBehavior* cb = Citizens();
    const ComponentDescription* keyObj = AsType<ComponentDescription>(ext);

    int bucketCount = 0;
    {
        TemplateBucket* node = cb->field_0x160._root;
        while (node) {
            if (keyObj < node->element.key) node = node->left;
            else if (keyObj <= node->element.key) { bucketCount = node->element.entities._size; break; }
            else node = node->right;
        }
    }
    int result = 0;
    for (int bi = 0; bi < bucketCount; bi++) {
        TemplateBucket* node = cb->field_0x160._root;
        ComponentEntity* entRef = nullptr;
        while (node) {
            if (keyObj < node->element.key) node = node->left;
            else if (keyObj <= node->element.key) { entRef = node->element.entities._data[bi].Get(); break; }
            else node = node->right;
        }

        DestroyComponent* destroy = FinishedStanding(entRef);
        if (destroy == nullptr) continue;

        if (DestroyComponent_InState345_e710(destroy) == 0) {
            MapComponent* tier = entRef->FindComponent<MapComponent>();
            short s1 = tier->tileX1;
            short s2 = tier->tileY1;
            if (result < s1) result = s1;
            if (result < s2) result = s2;
        }
    }
    return (uint32_t)result;
}

// 0x1400d9650 — gather the entities of three CitizenBehavior lists into one
// flat array, then for each (optionally filtered by template == ext) sum the
// stored amounts of its StorageComponent whose RawMaterialDescription flags
// intersect statFlags.
uint32_t StatGoal::SumStorageAmounts(const Object::External* ext) const {
    const ComponentDescription* owner = nullptr;
    if (ext != nullptr) owner = AsType<ComponentDescription>(ext);

    // Retail (0x1400d96ba): RAX = [g_pGameWorld]; RDX = [RAX+0xf0] — the
    // CitizenBehavior POINTER stored at +0xf0.
    CitizenBehavior* cb = Citizens();
    // A flat array of the three lists' entities: +0x00, +0x10, +0x30.
    int total = 0;
    {
        int c0 = cb->field_0x00._size, c1 = cb->field_0x10._size, c2 = cb->field_0x30._size;
        total = c0 + c1 + c2;
    }
    ComponentEntity** gathered = nullptr;
    if (total > 0) {
        gathered = static_cast<ComponentEntity**>(System::MemoryAllocator::MemAlloc((uint64_t)total * 8, 8));
    }
    int gi = 0;
    if (gathered) {
        int c0 = cb->field_0x00._size;
        for (int i = 0; i < c0; i++) gathered[gi++] = cb->field_0x00._data[i].Get();
        int c1 = cb->field_0x10._size;
        for (int i = 0; i < c1; i++) gathered[gi++] = cb->field_0x10._data[i].Get();
        int c2 = cb->field_0x30._size;
        for (int i = 0; i < c2; i++) gathered[gi++] = cb->field_0x30._data[i].Get();
    }

    int result = 0;
    for (int i = 0; i < gi; i++) {
        ComponentEntity* entity = gathered[i];
        if (owner != nullptr) {
            if (owner != entity->_description) continue;
        }
        StorageComponent* storage = entity->FindComponent<StorageComponent>();
        int childN = storage->storageSlots._size;
        for (int ci = 0; ci < childN; ci++) {
            const StorageComponent::Item& slot = storage->storageSlots[ci];
            RawMaterialDescription* prof = FindSubDescription<RawMaterialDescription>(slot._resource);
            uint32_t selfFlags = (uint32_t)statFlags;
            uint32_t profFlags = prof->_flags;   // +0x5c, no test
            if ((selfFlags & profFlags) != 0) {
                result += slot._count;
            }
        }
    }
    if (gathered) System::MemoryAllocator::MemFree(gathered);
    return (uint32_t)result;
}

// ============================================================
// Dispatcher 0x1400da4e0
// ============================================================
uint32_t StatGoal::StatValue(uint32_t statType, const Object::External* ext) const {
    GameUpdateLoop* loop = GameUpdateLoop::_singleton;
    switch (statType) {
    case 0:    return Helper_a410();
    case 1:    return Helper_a2a0(ext);
    case 2:    return Helper_a150(ext);
    case 3:    return Helper_a0a0();
    case 4:    return Helper_9ff0();
    case 5:    return (uint32_t)loop->m_citizenList->citizens._size;
    case 6:    return Helper_9f20();
    case 7:    return Helper_9e80();
    case 8:    return Helper_9dc0();
    case 9:    return Helper_9cd0(ext);
    case 0xa:  return Helper_9c80(ext);
    case 0xb:  return Helper_9c10(ext);
    case 0xc:  return Helper_9b80();
    case 0xd:  return (uint32_t)loop->m_citizenList->field_0x1AC;
    case 0xe:  return SumStorageAmounts(ext);
    case 0xf:  return Helper_9560();
    case 0x10: return Helper_9470();
    case 0x11: return Helper_9380();
    case 0x12: return Helper_92d0(ext);
    case 0x13: return CountDistinctTracked(ext);
    case 0x14: return SumProductionWindow();
    case 0x15: return SumTrackedForProfession(ext);
    case 0x16: return Helper_86e0(ext);
    case 0x17: return ReadSubTypeEntry();
    case 0x18: return Helper_85f0(ext);
    case 0x19: return Helper_81b0(ext);
    case 0x1a: return (uint32_t)loop->m_dayCounter->year;
    case 0x1b: return (uint32_t)loop->m_mapSize;
    case 0x1c: return (uint32_t)loop->m_citizenList->field_0x1B0;
    case 0x1d: return Helper_7cf0(ext);
    default:   return 0;
    }
}
