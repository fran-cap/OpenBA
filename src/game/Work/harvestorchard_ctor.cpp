// src/game/Work/harvestorchard_ctor.cpp
// HarvestOrchard "from entity" constructor — retail HarvestOrchard_Ctor (0x1400ee060).
//
// WHY A SEPARATE FILE: harvestorchard.cpp hosts the vtable and every slot body but not
// this constructor — the same split plantorchard_ctor.cpp / tendarea_ctor.cpp exist for.
// It is the last unported leaf in OrchardComponent state 11's closure: the state-11 enter
// (0x140050800) builds the harvest work item with
// `HarvestOrchard_Ctor(MemAlloc(0x68, 8), &orchard->+0x10)` — note NO tree-type argument,
// unlike PlantOrchard_Ctor.
#include "Work/exe_bodies.h"
//
// ABI, read from the decompile at 0x1400ee060 (not inferred):
//   HarvestOrchard* __fastcall HarvestOrchard_CtorFromEntity(void* mem /*RCX*/,
//                                                            void** entityIBP /*RDX*/)
// Retail copies the incoming IBP to a local FIRST and hands Work_Ctor25 the address of
// that local, because Work_Ctor25 takes an InternalBasePtr* rather than a raw pointer.
//
// The tail is what makes the harvest work item meaningful: it sizes its own per-slot byte
// array from the orchard's CURRENT slot count and marks 1 for every slot that has a tree
// bound (orchard slot +0x08 non-null), 0 for every hole. That byte array is the harvester's
// worklist.
//
// ⚠ Retail writes the vtable TWICE — first vtable_AutoPickupWork, then
// vtable_HarvestOrchard. That is the inlined base constructor, not a bug; reproduced.

#include "Work/HarvestOrchard.h"
#include <new>
#include "object/TypeInfo.h"
#include <cstdint>
#include <cstdio>
#include "Component/OrchardComponent.h"
#include "Entity/ComponentEntity.h"

namespace {
// 0x140005440 is WorkAcc_FindComponent_5440 (Work/exe_bodies.h), called by name (B3R).
// 0x14000a590 (Array<bool>::SetSize) is the exe-side Collection::Array<T>
// monomorphisation: `_slotsAvailable.SetSize` below.
// ENDGAME B.3.8: the file-level lazy Resolve() is gone (B38 D7) — Work_Ctor25 is the
// base ctor Work(void*), the two stamps are the compiler's (the old AutoPickupWork
// stamp read 0x140161380, an RTTI descriptor, and was always skipped), and the finder
// is resolved where it is used.
}  // namespace

#include "orchard_diag.h"   // standalone/: the orchard diagnostic counters

// Retail copies the incoming IBP to a local and hands Work_Ctor25 its address; Work(void*)
// reads the pointer once, so passing the caller's IBP is the same read.
HarvestOrchard::HarvestOrchard(ComponentEntity* const* entityIBP) : AutoPickupWork(entityIBP) {
    OrchardDiag_NoteHarvestConstructed();   // the port's diagnostic counter (no-op when off)
    // +0x48 / +0x50: the `_cells` member's ctor zeroes the set, then `_slotsAvailable`'s ctor
    // zeroes +0x58 / +0x60 (retail 0x1400ee0ae..0x1400ee0b8, under its EH state).

    // The entity's OrchardComponent (0x140005440)
    // B6 (14d G47): 0x1400ee0df / 0x1400ee0e4 read the orchard's +0x58 with no test; the
    // loop (0x1400ee100-0x1400ee137) reads +0x50 and the work item's +0x58 each pass with
    // no test of the orchard, the byte array or the slot array.
    OrchardComponent* orchard = _entity->FindComponent<OrchardComponent>();

    // 0x14000a590 == Collection::Array<bool>::SetSize (no tail write: every slot is
    // written by the loop below).
    _slotsAvailable.SetSize(orchard->slotCount);

    // One byte per orchard slot: 1 = there is a tree here to pick, 0 = hole. Retail
    // re-runs CheckPtr on the orchard IBP every iteration and loops on the WORK item's
    // own count (+0x60) — the value the resize just produced — not the component's.
    for (int i = 0; i < _slotsAvailable._size; ++i) {
        OrchardSlot* slots = orchard->slots;
        bool* base = _slotsAvailable._data;
        base[i] = (slots[i].tree != nullptr) ? 1 : 0;
    }
}
