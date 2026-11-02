// src/game/Component/citizen_assignworkplace.cpp
// Phase-4 economy: faithful 1:1 port of CitizenComponent::AssignWorkplace (SetHome), so
// standalone mode can set a citizen's home (+0x238 / +0x240) and keep the residence's
// family roster in sync.
//
//   0x1401163d0  CitizenComponent_AssignWorkplace  (set +0x238/+0x240; re-roster; recurse family)
//   (the roster: ResidenceComponent::RemoveResident 0x14012a550 / AddResident 0x14012a710 /
//    CountResidents 0x140129ff0, residencecomponent.cpp)
//
// DORMANT: no caller in code/Game yet (the lead wires it in a later brick). It must be
// crash-safe and byte-faithful now. The roster bookkeeping (find/compact/decrement in
// a550; grow-array append + qsort in a710; the count sum in 129ff0) is the FAITHFUL CORE
// and runs unconditionally. Every rendering/audio side effect is INERT — in standalone
// the Audio::Scene (*(gameWorld+0x78)) is often null and FindComponent can return a
// null-wrapped IBP — so those branches are guarded to no-op crash-safe, mirroring the
// established inert-branch pattern (src/game/Component/citizencomponent.cpp roster read,
// reference_citizencomponent_field_map).
//
// Verified against Application-x64.exe disassembly (Ghidra):
//   AssignWorkplace ABI  RCX=self, RDX=workplaceIBP, R8D=slotIndex (0x1401163e2 MOV EBP,R8D)
//   a550/a710 ABI        RCX=storageComp, EDX=slotIndex, R8=selfIBP
//   Roster table         storage+0x50 -> array of {int64 data; int32 count; int32 cap} (stride 0x10)
//                        storage+0x58 = profession count (129ff0 loop bound)
//   Worker element       raw 8-byte component pointer (find stride 8; compact stride 8)
//   Citizen fields       +0x238 workplace IBP, +0x240 slotIndex, +0x260 family head,
//                        +0x268 family next, +0x258 (=600) parent/guardian
//   Dispatch slots       0x140152ab0 CheckPtr, 0x140152a90 IBP::ctor (via engine globals),
//                        0x140152a70 Vector3::operator==, 0x140152a00 MoveEmitter,
//                        0x140152a08 SetEmitterVolume, 0x140152a10 StopEffect3D,
//                        0x140152a18 GetEmitterCount, 0x140152a20 RemoveEmitter
//

#include "err_line.h"   // HITCH_POSTDRAIN: one write per diagnostic line
#include <new>
#include "math/Math.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "platform_os.h"   // Platform::ExecutableImageBase (the [Unassign] line; Phase D.4 LINUX-1)
#include <cstdio>
#include <cstdint>
#include <cstdlib>   // qsort
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "audio/Audio.h"       // ENDGAME B.3.7 callers (K4)
#include "Component/exe_bodies.h"
#include "Component/CitizenComponent.h"     // B.4 W1 COMP-b: SetHome is a member
#include "Component/ResidenceComponent.h"
#include "Entity/ComponentEntity.h"          // FindComponent<T>
#include "Component/firecomponent_funcs.h"
#include "Action/exe_bodies.h"
#include "Work/exe_bodies.h"
#include "Game/exe_constants.h"
#include "ext/registry.h"                      // EXT SEAM (NOT RETAIL): Ext::Fix().stranded_wade
#include "ext/stranded_wade/stranded_wade.h"   // EXT SEAM (NOT RETAIL, docs/retail_bugs.md §19)

// The residence roster helpers this file used to hold (0x14012a550 / 0x14012a710 /
// 0x140129ff0 / the 0x140129e20 comparator) are ResidenceComponent members now
// (residencecomponent.cpp): their receiver is the ResidenceComponent.


// ============================================================
// CitizenComponent::SetHome (0x1401163d0; tree and Ghidra: "AssignWorkplace")
//   home = the new home entity (the residence), slot = the household slot in it.
// If the assignment actually changes (+0x238 or +0x240 differ): remove from the old
// residence's household roster, store the new home + slot, add to the new roster, then
// apply the same assignment to every child on the +0x260/+0x268 circular ring.
// The rosters are ResidenceComponent's (RemoveResident 0x14012a550 / AddResident 0x14012a710).
// ============================================================
void CitizenComponent::SetHome(ComponentEntity* newHome, int32_t slotIndex) {
    void* selfV = this;
    Object::InternalPtr<ComponentEntity>* pWorkplace = &home;  // current home
    int32_t* pSlot = &homeSlot;
    ComponentEntity* newWp = newHome;

    if (pWorkplace->Get() == newWp && *pSlot == slotIndex)
        return;   // no change

    // EXT SEAM (NOT RETAIL, docs/retail_bugs.md §19): fix.stranded_wade only observes the change: a
    // lost home is remembered as a homeless stranded citizen's anchor (src/ext/stranded_wade/).
    if (Ext::Fix().stranded_wade) Ext::StrandedWade::OnHomeChange(this, pWorkplace->Get(), newWp);

    // [diag] A citizen losing its home is how "everyone froze after a load"
    // starts: +0x238 goes null, the AI has nothing to plan, and the town starves.
    // Only the UN-assignment is interesting, and only its CALLER identifies which
    // rule decided the home was invalid -- so report the return address and let nm
    // name it (image base 0x140000000).
    if (pWorkplace->Get() != nullptr && newWp == nullptr) {
        static int n = 0;
        if (n < 25) {
            ++n;
            ErrLine("[Unassign] #%d citizen=%p oldWp=%p slot %d->%d "
                         "caller=%p (static 0x%llx)\n", n, selfV,
                         (void*)pWorkplace->Get(), *pSlot, slotIndex,
                         __builtin_return_address(0),
                         (unsigned long long)((uintptr_t)__builtin_return_address(0)   // CAST-OK: ptr_int
                             - Platform::ExecutableImageBase() + 0x140000000ULL));
            std::fflush(stderr);
        }
    }

    // --- Remove from the old residence roster. ---
    if (pWorkplace->Get() != nullptr) {
        ResidenceComponent* storage = pWorkplace->Get()->FindComponent<ResidenceComponent>();
        Object::InternalPtr<CitizenComponent> selfIBP(this);
        // 0x14011641f-0x14011644d: the find's result goes to RemoveResident untested
        // (B8 RES, row 310): an assigned home always carries a ResidenceComponent.
        storage->RemoveResident(*pSlot, &selfIBP);   // 0x14012a550
    }

    // --- Store the new home + slot. ---
    *pWorkplace = newWp;
    *pSlot = slotIndex;

    // --- Add to the new residence roster. ---
    if (pWorkplace->Get() != nullptr) {
        ResidenceComponent* storage = pWorkplace->Get()->FindComponent<ResidenceComponent>();
        Object::InternalPtr<CitizenComponent> selfIBP(this);
        storage->AddResident(*pSlot, &selfIBP);   // 0x14012a710 (0x140116475-0x1401164a3, untested)
    }

    // --- Propagate to the children on the +0x260 circular ring. ---
    // head = _firstChild; each node's next = _nextSibling; loop until next wraps to head.
    CitizenComponent* node = _firstChild.Get();
    if (node != nullptr) {
        do {
            node->SetHome(newHome, *pSlot);
            node = node->_nextSibling.Get();
        } while (node != _firstChild.Get());
    }
}

