// src/game/Work/gatherrawmaterials_ctor.cpp
// Clean C++ port of GatherRawMaterials_Ctor @ 0x1400ec710.
//
// Constructs a GatherRawMaterials Work (size 0x78, vtable 0x140161420) over a
// caller-allocated 0x78 block, given the build-site entity InternalBasePtr.
// The exe calls it as: GatherRawMaterials_Ctor(MemAlloc(0x78), &buildEntityIBP).
//
// Layout produced (see engine/types/BuildActions.h : GatherRawMaterials):
//   +0x00 vtable = 0x140161420
//   +0x10..0x40 Work base (initialised by Work_Ctor25 @0x1400f72e0)
#include "Work/exe_bodies.h"
//   +0x48 int[] ptr  / +0x50 count(=matCount)  "amount already present per material"
//   +0x58 int[] ptr  / +0x60 count(=matCount)  "delivered per material" (all 0)
//   +0x68 BST root (0) / +0x70 count (0)
//
// For each material in the build entity's BuildDescription, _present[i] is seeded
// with the amount ALREADY in the site StorageComponent (slot.total - slot.reserved),
// and _inFlight[i] with 0.  (Execute() treats _present as the per-material
// already-assigned/present count and _inFlight as the in-flight build count.)
//
// Faithful offset-by-offset port. Verified against Ghidra disassembly of
// 0x1400ec710 and callees: Work_Ctor25 @0x1400f72e0, FUN_14000d6c0 @0x14000d6c0
// (find BuildComponent), Entity_FindComponent_Storage @0x140006270,
// FUN_1400021d0 @0x1400021d0 (int-array resize; sets ptr@+0 and count@+8).
//
// Consistent with the GatherRawMaterials struct + field conventions used by
// src/game/Work/gatherrawmaterials.cpp (which we do NOT modify).

#include "Work/GatherRawMaterials.h"
#include <new>
#include "object/TypeInfo.h"
#include "Dialog/dialog_exe_helpers.h"   // B3R: Dialog_FindComponent_d6c0
#include <cstdint>
#include "Component/BuildDescription.h"   // ENDGAME B.3.8 CB3: BuildDescription::TypeId()
#include "Component/BuildComponent.h"
#include "Component/StorageComponent.h"
#include "Entity/ComponentEntity.h"

#include "Object/factory_registration.h"

// FindComponent that returns the BuildComponent (holds BuildDescription at +0x18):
// 0x14000d6c0 Dialog_FindComponent_d6c0 (Dialog/dialog_exe_helpers.h).
// FindComponent that returns the site StorageComponent (type 0x1401c7e78):
// 0x140006270 WorkBase_FindComponent_6270 (Work/exe_bodies.h). Both called by name (B3R).
// 0x1400021d0 (int-array SetSize) is the exe-side Collection::Array<int32>
// monomorphisation -- ENDGAME B.3.2 step 2 converted its two call sites below to
// member calls (now of the two Array<int32_t> members), so its resolved-pointer stand-in
// is gone.

// "BuildDescription" type-id (DWORD_1401c7040). Name verified via the .rdata
// writer BuildDescription_Register (0x14014e5f5). GetTypeIdByName in standalone
// (the raw .rdata global is null).
// ENDGAME B.3.8: read per call (the function-local lazy cache is gone, B38 D7).
static uint32_t GRMC_BuildDescTypeId() {
    return BuildDescription::TypeId();
}

// The base ctor is Work(buildEntityIBP) = Work_Ctor25 (0x1400f72e0) on a temp IBP
// holding the raw build-entity pointer (it reads *param only, into _entity); the two
// stamps are the compiler's.
GatherRawMaterials::GatherRawMaterials(ComponentEntity* const* buildEntityIBP) : Work(buildEntityIBP) {
    // The +0x48 / +0x58 arrays and the +0x68 set are members: their ctors zero them (retail
    // 0x1400ec752..0x1400ec771: four qwords, then the set's root qword and count dword).

    // --- Locate BuildComponent (-> BuildDescription at +0x18) on the build entity ---
    // (the entity is *buildEntityIBP, which the Work base stored in _entity).
    // 0x1400ec788 CALL 0x14000d6c0, 0x1400ec7a3 [RAX+0x18] unguarded; IsDerivedFrom
    // (0x1400ec7b0) ? desc : null (CMOVNZ, 0x1400ec7bb).
    BuildComponent* buildComp = _entity->FindComponent<BuildComponent>();
    Description* descObj = buildComp->_description;
    BuildDescription* desc = descObj->IsDerivedFrom(GRMC_BuildDescTypeId())
                                 ? static_cast<BuildDescription*>(descObj) : nullptr;

    // --- Locate the site StorageComponent (0x1400ec7d3 CALL 0x140006270) ---
    StorageComponent* storage = _entity->FindComponent<StorageComponent>();

    // --- Size the two parallel int arrays to the material count ---
    // 0x1400ec7d8 / 0x1400ec7e5 read desc+0x28 with no test (a non-BuildDescription faults,
    // as retail).
    // FUN_1400021d0 == Collection::Array<int32>::SetSize (retail
    // GatherRawMaterials_Ctor calls it at 1400ec7e0 and 1400ec7ed, the member's own SetSize; no init arm) — a grown
    // tail is left as the allocation found it, like retail. Safe: the seeding loop right
    // below (retail 1400ec800..1400ec87f) writes BOTH arrays at every index i < +0x50
    // before either is read.
    _present.SetSize(desc->_costCount);
    _inFlight.SetSize(desc->_costCount);

    // --- Seed _present[i] = amount already present in site storage, _inFlight[i] = 0 ---
    // (0x1400ec800-0x1400ec87f: no test on the cost array, the storage or the int arrays)
    for (int i = 0; i < _present._size; i++) {
        ComponentDescription* matType = desc->_costs[i].material;

        // delivered = matching site-storage slot (total - reserved), else 0.
        int delivered = 0;
        for (int s = 0; s < storage->storageSlots._size; s++) {
            const StorageComponent::Item& slot = storage->storageSlots[s];
            if (slot._resource == matType) {
                delivered = slot._count - slot._reserve;   // +0x08 - +0x0c
                break;
            }
        }

        // NOTE: the exe stores the *current storage amount* into _present[i] (NOT a
        // costQty shortfall) — verified from disassembly. _inFlight[i] starts at 0.
        _present[i] = delivered;
        _inFlight[i] = 0;
    }

}
