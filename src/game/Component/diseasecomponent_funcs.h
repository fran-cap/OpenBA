#pragma once
// Declarations for the ported DiseaseComponent bodies
// (src/game/Component/diseasecomponent.cpp).
//
// ⚠ DiseaseComponent is NOT citizen sickness. Citizen illness is the `Disease`
// resource driven by CitizenComponent::UpdateHealth (citizen_updatehealth.cpp).
// DiseaseComponent is the per-BUILDING component behind retail Banished's crop
// INFESTATION disaster — attached to crop fields, orchards and pastures:
//   * a field becomes infested (seasonal roll, weighted by whether the crop was
//     rotated since the last cure),
//   * its plants die one at a time with their yield zeroed — or, on a pasture,
//     its animals die one at a time,
//   * it spreads to nearby entities growing the SAME crop,
//   * harvesting / clearing / re-cropping the field cures it.
//
// Vtable 0x140159290. Size 0x58. Slot numbering was taken from the VTABLE BYTES,
// not Ghidra's symbol names, which are shifted by one slot for this class:
//   [3] Serialize 0x140048ca0   [4] Startup 0x140048c50   [6] OnAdded 0x140048c00
//   [7] OnRemoved 0x140048bb0   [9] Update  0x140048b90

#include "Component/DiseaseComponent.h"
#include "Component/DiseaseDescription.h"
#include <cstdint>


// --- behaviour bodies: DiseaseComponent members (DiseaseComponent.h) -------

// --- shared leaves ----------------------------------------------------------
// "Is the crop recorded at the last cure still planted on my tiles?" — the
// crop-rotation mechanic. Compares tile cell+0x10 (the grown-resource identity
// raster) across the footprint.
class ComponentDescription;
class ComponentEntity;
// 0x14009fb60 is Map::HasIdentityInRect (Game/Map.h), defined in diseasecomponent.cpp.
// What this entity is currently growing (pasture livestock / tile plant / the crop
// the field is merely SET to): retail returns ExternalPtr<ComponentDescription> by
// value (RCX) from an InternalPtr<ComponentEntity> (RDX).
extern "C" ComponentDescription** DiseaseComp_GrownIdentity(ComponentDescription** out,  // B8: free in retail -- RCX is the out slot (0x140048375 LEA RCX,[RSP+0x40]), RDX the entity slot
                                                           const Object::InternalPtr<ComponentEntity>* entitySlot); // 0x140047f80
// 0x1400050a0 (the DiseaseComponent finder) is ComponentEntity::FindComponent<DiseaseComponent>;
// 0x1400537f0 is ParticleComponent::StopEmitterFade (ParticleComponent.h).
