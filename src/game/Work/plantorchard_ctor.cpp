// src/game/Work/plantorchard_ctor.cpp
// PlantOrchard "from entity" constructor — retail 0x1400f5140.
//
// WHY A SEPARATE FILE: plantorchard.cpp already hosts the PlantOrchard vtable and every
// slot body, but NOT this constructor. It is the single unported leaf in state 9's
// transitive closure — the OrchardComponent state machine's state 9 and state 12 enter
// handlers both allocate 0x70 bytes and call this to build the planting work item. Kept
// out of plantorchard.cpp only to avoid an edit collision while that file is in use.
//
#include "Work/exe_bodies.h"
// ⚠ NOT to be confused with PlantOrchardAddr::Ctor (0x1400f50b0), which is the zero-arg
// deserialize/factory constructor and leaves +0x68 null. See the note in BuildActions.h.
//
// ABI (verified against the disassembly, not inferred):
//   PlantOrchard* __fastcall PlantOrchard_CtorFromEntity(
//       void* mem /*RCX*/, void** entityIBP /*RDX*/, void* treeType /*R8*/)
// Called as PlantOrchard_CtorFromEntity(MemAlloc(0x70, 8), &orchard->+0x10,
//                                      *(orchard+0x60)).
//
// Retail copies both incoming arguments to locals FIRST (`local_res20 = *param_2;
// local_res18 = param_3`) and hands Work_Ctor25 the address of that local, because
// Work_Ctor25 takes an InternalBasePtr* rather than a raw pointer. Reproduced exactly:
// passing &entityIBP straight through would hand it the caller's storage.

#include "Work/PlantOrchard.h"
#include <new>
#include "object/TypeInfo.h"
#include "Component/exe_bodies.h"   // B3R: OrchardComponent_ValidateRefs
#include "Component/OrchardComponent.h"
#include "Entity/ComponentEntity.h"
#include <cstdint>
#include <cstdio>

namespace {
// 0x140005440 WorkAcc_FindComponent_5440 (Work/exe_bodies.h) and 0x140051830
// OrchardComponent_ValidateRefs (Component/exe_bodies.h) are called by name (B3R).
// 0x14000a590 (Array<bool>::SetSize) is the exe-side Collection::Array<T>
// monomorphisation: `field_58.SetSize` below.
// ENDGAME B.3.8: the file-level lazy Resolve() is gone (B38 D7) — Work_Ctor25 is the
// base ctor Work(void*), the stamp is the compiler's, and the two helpers are resolved
// where they are used.
}  // namespace

// The diag counter is bumped in the FACTORY ctor only, so this path — the state machine's,
// and the only one that actually runs in a live game — reported "constructed=0" while
// building work items every time. Count here too, or the counter reads as a failure of the
// thing it is meant to be measuring.
#include "orchard_diag.h"   // standalone/: the orchard diagnostic counters

// Retail copies the incoming IBP to a local and hands Work_Ctor25 its address; Work(void*)
// reads the pointer once, so passing the caller's IBP is the same read.
PlantOrchard::PlantOrchard(ComponentEntity* const* entityIBP, ComponentDescription* treeType) : Work(entityIBP) {
    OrchardDiag_NotePlantConstructed();   // the port's diagnostic counter (no-op when off)
    // PlantOrchard's own fields. Retail writes +0x60 as one 8-byte store, which clears
    // BOTH the count (+0x60) and the capacity (+0x64) of the array triple at +0x58:
    // here the two named fields, in address order.
    // +0x48 / +0x50: the `_cells` member's ctor zeroes the set, then `field_58`'s ctor
    // zeroes +0x58 / +0x60 (retail 0x1400f50b0, under its EH state).
    field_68 = treeType;   // tree type, stored verbatim: no null test, no validation,
                           // no refcount — as retail does.

    // Read unguarded (0x1400f51b1 -> 0x1400f51c6): a PlantOrchard is only built for an
    // entity that has an orchard.
    OrchardComponent* orchard = field_10->FindComponent<OrchardComponent>();   // 0x140005440

    orchard->ValidateRefs();   // 0x140051830

    // Size the per-slot validity array from the orchard's CURRENT slot count.
    // 0x14000a590 == Collection::Array<bool>::SetSize (no tail write: the loop below
    // writes every slot).
    const int slotCount = orchard->slotCount;
    field_58.SetSize(slotCount);

    // Zero each byte. Retail re-reads orchardComp->+0x58 and re-runs CheckPtr on every
    // iteration; the re-read is preserved because ValidateRefs/CheckPtr can in principle
    // shrink the component's slot array underneath the loop. No data test
    // (0x1400f5200..0x1400f5221): SetSize allocated whenever the loop runs.
    for (int i = 0; i < orchard->slotCount; ++i) {
        bool* base = field_58._data;
        base[i] = false;
    }
}
