// src/game/Component/citizencomponent_lifecycle.cpp
// Citizen-lifecycle quartet (openfuncs step 2.5a) — sibling of the (huge)
// citizencomponent.cpp. Faithful ports, Ghidra-verified against Application-x64.exe:
//
//   0x140112040  CitizenComp_LeaveRoster_2040     — unlink from the workplace worker
//                roster (doubly-linked ring at node +0x268/+0x270, head cache self
//                +0x260), then clear the workplace slot (AssignWorkplace(null,-1)) and
//                the current-workplace link (+0x258).
//   0x1401168a0  CitizenComp_JoinRoster_68a0      — inverse: splice a citizen into the
//                roster before the head (or seed a singleton ring), then read the
//                workplace descriptor (self +0x238 IBP / +0x240 slot) and AssignWorkplace.
//   0x1400302a0  CitizenComp_SortByProfession_02a0 — sort an entity-handle array by
//                profession priority (see note on the sort below).
//   0x140117080  CitizenComp_DeferredInit_117080  — per-citizen deferred init:
//                name/age/gender/appearance-variant/lifespan/housing/employment.
//
// Registered by standalone/reg_citizen_25.cpp; the callers already ResolveAddr these
// four addresses in citizencomponent.cpp (s_fn_sub_2040 / s_fn_helper_68a0 /
// s_fn_helper_02a0 / s_fn_FUN_140117080), so registering wires them live.
//
// The 0x1400302a0 sort is the shipped MSVC sort skeleton (rtsort::Introsort,
// collection/introsort_msvc.h) over 8-byte entity handles, comparator 0x140110330
// (`prio(a) < prio(b)`) and, for ranges of 8 or fewer, the max-to-high selection sort
// 0x140030b90 (Ghidra's "InsertionSortByProfession" is a misnomer). Comparator and
// fallback both reduce a handle to a profession priority (resolve the entity's
// WorkComponent, read its profession key at comp+0x28, look that key up in the world
// priority RB-tree at GameWorld+0x118 and read priority at record+0x24). The sort is
// unstable and workplaces of equal priority are common, so the swap sequence decides
// their order: it is retail's (rtsort::SelectionSortMaxToHigh for the fallback), not
// std::sort's. The shared priority predicate is ProfessionPriorityOf below.

#include <new>
#include "Game/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/exe_constants.h"   // kRdata_* (B3R Leftovers: the .rdata constants)
#include "system/String.h"
#include "collection/introsort_msvc.h"   // rtsort::Introsort (0x1400302a0) / SelectionSortMaxToHigh (0x140030b90)
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/CitizenComponent.h"
#include "Component/CitizenDescription.h"   // B.4 W1 COMP-b: description fields by name
#include "Component/ModelComponent.h"
#include "Entity/ComponentEntity.h"          // FindComponent<T>
#include "Component/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "Player/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Component/WorkComponent.h"   // B4 P2 CIT-4: WorkComponent::workId
#include "Game/WorkScheduler.h"        // B4 P2 CIT-4: GameUpdateLoop::m_tutorial trees
#include "Game/WorkSchedulerNodes.h"   // B4 P2 CIT-4: WorkSchedulerIdNode
#include "Work/Work.h"                 // B4 P2 CIT-4: Work::field_24
#include "Component/CharacterComponent.h"   // B4 S4 RT2: InternalPtr<CharacterComponent>::Get() needs the class complete

// Clean C++ name generator (src/game/Game/citizen_names.cpp) — replaces the retail
// name-gen chain (FUN_1400a4c60/a4b30/a4880) that 0x140117080 would otherwise call.
extern "C" bool GenerateCitizenName(System::Random* rng, bool female, char* out, int cap);

// Exe sub-callees are called by name (Component/Work/Player/Action exe_bodies.h).

// ============================================================
// CitizenComponent::RemoveChild (0x140112040)
// Unlink `child` from this parent's children ring (ring links child +0x268/+0x270,
// head cache this +0x260), re-init its own links to empty, take it out of its home
// (SetHome(null, -1)) and clear its +0x258 parent link.
// ============================================================
void CitizenComponent::RemoveChild(CitizenComponent* child) {
    CitizenComponent** ibp = &child;   // *ibp == the child

    if (_firstChild == *ibp) {
        _firstChild = (*ibp)->_nextSibling;
    }
    // node.prev.next = node.next
    CitizenComponent* nextLink = (*ibp)->_nextSibling.Get();
    CitizenComponent* node = *ibp;
    node->_prevSibling->_nextSibling = nextLink;
    // node.next.prev = node.prev
    CitizenComponent* prevLink = (*ibp)->_prevSibling.Get();
    node = *ibp;
    node->_nextSibling->_prevSibling = prevLink;
    // node's own links -> empty
    (*ibp)->_prevSibling = nullptr;
    (*ibp)->_nextSibling = nullptr;
    // if this node was the head, clear the head cache
    if (_firstChild == *ibp) {
        _firstChild = nullptr;
    }
    // clear the child's home + its parent link (+0x258)
    (*ibp)->SetHome(nullptr, -1);
    (*ibp)->_parent = nullptr;
}

// ============================================================
// CitizenComponent::AddChild (0x1401168a0)
// Inverse of RemoveChild: splice `child` into the ring before the current head (or
// seed a singleton ring when empty), store this as the child's +0x258 parent, then
// move the child into the parent's home (+0x238 / slot +0x240).
// ============================================================
void CitizenComponent::AddChild(CitizenComponent* child) {
    CitizenComponent** ibp = &child;
    CitizenComponent* head = _firstChild.Get();
    CitizenComponent* v;
    CitizenComponent* node;

    if (head == nullptr) {
        // empty ring -> singleton ring (prev = next = node)
        _firstChild = *ibp;
        v = *ibp;
        (*ibp)->_nextSibling = v;
        v = *ibp;
        node = *ibp;
    } else {
        v = head->_prevSibling.Get();           // head.prev
        (*ibp)->_prevSibling = v;         // node.prev = head.prev
        v = _firstChild.Get();                  // head
        (*ibp)->_nextSibling = v;         // node.next = head
        v = *ibp;
        node = *ibp;
        node->_prevSibling->_nextSibling = v;   // node.prev.next = node
        v = *ibp;
        node = *ibp;
        node = node->_nextSibling.Get();        // node.next
    }
    node->_prevSibling = v;

    // store this as the child's parent (+0x258), then move it into the parent's home
    (*ibp)->_parent = this;
    CitizenComponent* slotHost = (*ibp)->_parent.Get();
    ComponentEntity* wpIBP = (*ibp)->_parent->home.Get();
    (*ibp)->SetHome(wpIBP, slotHost->homeSlot);
}

// ============================================================
// Profession-priority predicate (shared by comparator 0x140110330 and insertion-sort
// fallback 0x140030b90). Resolve the entity's WorkComponent, read its profession key
// (comp+0x28), then look that key up in the world priority RB-tree (GameWorld+0x118).
// ============================================================
// As retail (0x140110330): the WorkComponent, the world, the scheduler (+0x118) and the
// found Work are used with no test; every workplace candidate carries a WorkComponent
// whose id the scheduler registered (citizenai_work_scheduler.cpp).
static int ProfessionKeyOfEntity(ComponentEntity* handle) {
    WorkComponent* comp = handle->FindComponent<WorkComponent>();   // 0x140003050
    return comp->workId;
}

// GameWorld+0x118 is the work scheduler; its trees[0] (+0x30) is keyed on the work id
// (WorkSchedulerIdNode: key at +0, left +0x18, right +0x20, the Work at +0x8), the
// priority at Work +0x24.
static int LookupProfessionPriority(int key) {
    WorkScheduler* tree = GameUpdateLoop::_singleton->m_tutorial;
    WorkSchedulerIdNode* node = tree->trees[0].root;   // root
    while (node) {
        if (key < node->key)      node = node->left;    // left  (node+0x18)
        else if (key > node->key) node = node->right;   // right (node+0x20)
        else break;
    }
    // An absent key leaves a null IBP (0x140110420), and retail reads its +0x24 anyway.
    Work* value = node ? node->work.Get() : nullptr;   // node+0x8
    return value->field_24;
}

static inline int ProfessionPriorityOf(ComponentEntity* handle) {
    return LookupProfessionPriority(ProfessionKeyOfEntity(handle));
}

// ============================================================
// CitizenComp_SortByProfession_02a0 (0x1400302a0) — see file-header note.
// ============================================================
void CitizenComp_SortByProfession_02a0(ComponentEntity** array, int n, void* /*tmpV*/) {
    if (!array || n < 2) return;
    auto less = [](ComponentEntity* a, ComponentEntity* b) {   // 0x140110330
        return ProfessionPriorityOf(a) < ProfessionPriorityOf(b);
    };
    auto swp = [](ComponentEntity** a, ComponentEntity** b) {
        ComponentEntity* const t = *a;
        *a = *b;
        *b = t;
    };
    rtsort::Introsort(array, n, less, swp,
                      [&](ComponentEntity** lo, ComponentEntity** hi) {   // 0x140030b90
                          rtsort::SelectionSortMaxToHigh(lo, hi, less, swp);
                      });
}

// ============================================================
// CitizenComponent::Initialize (0x140117080)
// Per-citizen deferred init: (spawnPos, gender, isElder, age, forceEmploy, parent,
// profession). Callers: the birth path (GiveBirth), Game_SpawnCitizens (founding),
// the immigration states and the debug UI. Gives colonists real names/ages/lifespans.
// ============================================================
void CitizenComponent::Initialize(const Math::Vector3* pos, uint32_t gender, char isElder, float age,
                                  char forceEmploy, CitizenComponent* parent,
                                  Profession* spawnKey) {
    GameUpdateLoop* world = (GameUpdateLoop::_singleton != nullptr) ? GameUpdateLoop::_singleton : nullptr;
    System::Random* rng = world ? world->m_random : nullptr;

    // 1-3. reset path/target, set gender mesh variant, place at spawn transform
    ModelComponent* modelIBP = _entity->FindComponent<ModelComponent>();
    if (modelIBP) {
        modelIBP->SelectMesh(static_cast<uint16_t>(gender == 0), 0);   // 0x1401253d0
        modelIBP->SetTransformFromYaw(pos, 0.0f);   // 0x140003870
    }

    // 4. gender
    this->gender = static_cast<int32_t>(gender);

    // 5. random name -> +0x28 (faithful: temp String, operator=, ~String); `name` is
    // H5 raw storage for a System::String (CitizenComponent.h).
    char nameBuf[0x100] = {0};
    if (rng) GenerateCitizenName(rng, gender == 0, nameBuf, static_cast<int>(sizeof(nameBuf)));
    {
        System::String tmpStr(nameBuf);
        name = tmpStr;
    }

    // 6. appearance variant 0..7 — faithful (Rand()+Rand())&7 (two draws; (a%8+b%8)&7==(a+b)&7)
    if (rng)
        field_0x34 = static_cast<int32_t>((rng->Rand(8u) + rng->Rand(8u)) & 7);

    // 7. employment: unemployed unless forced or the world "start-employed" flag (bit 9) is set
    CitizenDescription* desc = description;
    uint32_t worldFlags = world ? world->m_gameFlags : 0;
    if (forceEmploy == 0 && ((worldFlags >> 9) & 1) == 0) {
        field_0x220 = false;
        education = 0;
    } else {
        field_0x220 = true;
        education = desc ? desc->maxEducation : 0;
    }

    // 8. age — a FLOAT in retail (the field the census and the adult-age gate compare
    // against desc+0x44). This argument was long declared `int`, which only worked
    // because every caller passed literal 0 (same bit pattern as 0.0f); the founding
    // spawn port (open H) passes real ages, so the typedef trap is now fixed at every
    // declaration (see NEXT_SESSION_BRIEF.md section 4).
    this->age = age;

    // 9. link into family (the parent IBP is dereffed once by the caller, as retail does)
    SetParent(parent);

    // 10. age-mesh setup
    if (isElder == 0) { SetupYoung(); }
    else              { SetupElder(); }

    // 11. workplace: (re)assign unless the profession and the workplace already match
    ComponentEntity* housingIBP = nullptr;
    bool needAssign = (profession != spawnKey);
    if (!needAssign) {
        needAssign = (workplace != housingIBP);
    }
    if (needAssign) {
        SetWorkplace(housingIBP);
        profession = spawnKey;
    }

    // 12. lifespan = RandFloatTol(rng) * desc+0x3c (age span) + desc+0x38 (base age)
    float rf    = rng ? rng->RandFloatTol() : 0.0f;
    float baseA = desc ? desc->_f38 : 0.0f;
    float spanA = desc ? desc->_f3c : 0.0f;
    maxAge = rf * spanA + baseA;

    // 13. sprite/anim init on the CharacterComponent (+0x278)
    character.Get()->SetAnimation(0,
              kRdata_14017214c, 0.0f);
}

// [11] 0x1401140f0 — the LOAD-TIME visual re-apply (clothing colour, age scale,
// carried tool), dispatched by the load-only EntityList pass.
// ============================================================
// CitizenComponent vtable slot 11 (+0x58) — retail 0x1401140f0
// ============================================================
// ⚠ NOT an update hook, despite this project's "OnUpdate2" naming for slot 11.
// It is the LOAD-TIME RE-APPLY, dispatched once per component by the load-only
// EntityList pass FUN_1400c4730 (ported as EntityList_PostLoad_c4730), which
// GameUpdateLoop::Serialize (0x140095a90) calls at 0x140095d3f on the LOAD leg.
//
// It is the ONLY thing that restores a citizen's age scale and CLOTHING COLOUR:
// SetupYoung/Adult/Elder write MeshInstance +0x10c..+0x114 (scale) and +0x118 =
// (float)citizen+0x34 (the colour index, itself `(Rand()+Rand()) & 7` at spawn).
// Graphics::MeshInstance::Serialize (Runtime 0x1800ff680) serializes ONLY the
// AnimationState at +0xd8 and the +0xc8/+0xd0 array — it does NOT round-trip the
// visual-scale Vector4 — and ModelComponent::Shutdown (0x140124b70) does not
// either. So after a load the colour MUST be re-applied, and this is what does it.
// It also re-shows the carried-tool mesh when the restored toolId (+0x1d8) != -1.
//
// Symptom while unwired: citizens come back with the WRONG clothes colours after a
// load, and only some of them. The mix is the giveaway: CitizenAIComponent::Update
// (0x14010f6a0) reaches CheckAge -> SetupAgeMesh only for an IDLE citizen — its tail
// is `if (charComp+0x60 >= 0) return;` where +0x60 is the action-stack top — so the
// few idle-on-that-tick citizens self-heal and the busy majority keep the
// MeshInstance ctor default, drifting back one at a time as they go idle.
//
// Byte-faithful to retail, including the unguarded +0x288 description deref.
void CitizenComponent::Slot11() {
    CitizenDescription* desc = description;
    const float   thresh = desc ? desc->adultAge : 0.0f;
    const float   age    = this->age;
    const bool    adult  = (school != nullptr);

    if (!adult) {
        if (age < thresh) { SetupElder(); }
        else              { SetupYoung(); }
    } else {
        SetupAdult();
    }

    // ⚠ KNOWN DEVIATION. Retail's 0x1401140f0 unconditionally re-shows the carried tool
    // here (Entity_impl_73b0 0x1401173b0) when the restored toolId != -1. Our port of
    // 73b0 CRASHES on that path at post-load time: it walks the tool description chain
    // (MeshInstance+0x28 -> deref -> deref) and faults on a READ of 0x100000001, i.e.
    // that link is not yet valid when the load-only slot-11 pass runs. 73b0's own header
    // already notes this is "the crash-prone path the ShowTool call site deliberately
    // guards off", so the gap is in 73b0, not here. Restore the re-show once 73b0's
    // description lookup is fixed.
}

// B4 X-G2-3: CitizenComp_JoinRoster_68a0 (the forwarder to CitizenComponent::AddChild) deleted:
// its one caller (starttown.cpp) calls the member.
