#pragma once
// ============================================================================
// src/game/Component/Component.h — ENDGAME B.3.8 (B38_RULES.md §2.1, D1-D3)
//
// `Component`, the exe's ECS component base, as a REAL C++ class whose vtable
// the compiler generates. Global namespace, as in retail (the reflected name is
// the bare "Component", parent literal "Object::Internal").
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §1, read 2026-09-26):
//   ctor   0x1401188b0  `??0Internal@Object@@` (IAT 0x140152768); stamp
//                       0x140162920; +0x10 = 0; +0x18 = 0; +0x20 = 0x80; then
//                       `if (+0x10 != 0) { InternalBasePtr ctor; 0x1400c3470 }`
//                       — DEAD, +0x10 was zeroed three instructions earlier
//                       (the inlined flag-change notify; see SetFlags below).
//   table  0x140162920, 16 slots, NO pure slot, create = 0x140003730 (abstract)
//   [0] 0x140031cc0 scalar deleting dtor: stamp 0x140162920, `~Internal`
//                   (IAT), `if (flags & 1) MemFree`. ~Component has no body.
//   [1] TypeName   [2] Type
//   [3] 0x1401189b0 Persist: `Internal::Persist`, Serializer(stream,0),
//                   InternalPtr +0x10 (entity; nulled unless IsDerivedFrom
//                   ComponentEntity), dword +0x20, 0x140031d10(+0x18), ~Serializer
//   [4]-[12], [14], [15]  shared `RET` no-ops (0x140118a50 … 0x140118ae0)
//   [13] 0x140118ac0 `XOR EAX,EAX; RET`
//
// LAYOUT: Object::Internal (0x10) + three fields = 0x28. The old POD
// (engine/types/Component.h, now a forwarder to this file) called +0x08 `next`
// and +0x10 `owner`: +0x08 is Object::Internal's `_factoryIndex/_generation`
// pair, and +0x10 is the owning ComponentEntity.
//
// SLOT NAMES 4-15 have no retail symbol (the exe has none). They are the tree's
// names from the slot stores (B38_RULES §2.1). ARGUMENT LISTS of 8, 9, 12-15
// are the tree's C bodies' parameter lists, NOT read from retail callers —
// B38_RULES §6 NOT VERIFIED; the owning block reads each caller before it
// relies on an argument.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "object/Internal.h"
#include "Object/exe_type.h"   // DECLARE_EXE_TYPE (ENDGAME B.3.9 RF)
#include "Component/ComponentFlags.h"   // ComponentFlags: the _flags (+0x20) bits

class ComponentEntity;   // Entity/ComponentEntity.h (the +0x10 owner)
class Description;       // Component/Description.h (the +0x18 reference)

class Component : public Object::Internal {
    DECLARE_EXE_TYPE(Component)

public:
    // ---- the 16 slots, in retail order -------------------------------------
    // No body in retail (slot 0 only re-stamps the table before ~Internal).
    ~Component() override = default;                             // [0]  0x140031cc0
    // [1] TypeName / [2] Type: the abstract base answers its own record
    // (DEFINE_EXE_TYPE, ENDGAME B.3.9 RF); every leaf overrides.
    // [1] TypeName 0x140031c40 and [2] Type 0x140031c30: DECLARE_EXE_TYPE (retail `MOV RAX,[ti+8]` / `MOV EAX,[ti]`).
    void        Persist(IO::Stream& stream) override;                   // [3]  0x1401189b0
    virtual void     Startup();                                   // [4]  0x140118a50 RET
    virtual void     Slot5();                                     // [5]  0x140118a60 RET
    virtual void     OnAdded();                                   // [6]  0x140118820 RET
    virtual void     OnRemove();                                  // [7]  0x140118830 RET
    virtual void     Tick(const System::Time* time);                            // [8]  0x140118a70 RET
    virtual void     Update(const System::Time* time);                          // [9]  0x140118a80 RET
    virtual void     PostUpdate();                                // [10] 0x140118a90 RET
    virtual void     Slot11();                                    // [11] 0x140118aa0 RET
    virtual void     OnEvent(int eventId);                        // [12] 0x140118ab0 RET
    virtual uint32_t CommandMask(int command);                    // [13] 0x140118ac0 ret 0
    virtual void     Command(int command);                        // [14] 0x140118ad0 RET
    virtual void     OnMessage(int message, void* data,
                               uint32_t arg0, uint32_t arg1);     // [15] 0x140118ae0 RET

    // 0x140118950: _flags = (_flags | orMask) & ~andNotMask, then the entity's flag
    // refresh (0x1400c3470) when the low word changed (defined in growthcomponent.cpp).
    void ModifyFlags(uint32_t orMask, uint32_t andNotMask);

    // ---- non-virtual members that read only the +0x10 entity (retail addresses; the
    // callers hand components of several classes) ----
    // 0x140129760: place this entity's MapComponent / ModelComponent at `cellIndex`,
    // turned `rotation` degrees (action_base_rawmat.cpp).
    void PositionRawMaterial(int cellIndex, float rotation);
    // 0x140107260: _flags |= 0x100000 (the entity's flag refresh on a low-word change),
    // then clear the CharacterComponent's running actions and Update(0) (action_pf_ctors.cpp).
    void FlagForRemoval();
    // 0x14004edb0: a random interact node of this entity, its reachable cost from
    // `goalCell` (-1 = unreachable), its position into `outPos` (action_pf_road.cpp).
    int  PickReachableCell(uint32_t goalCell, float* outPos) const;
    // 0x14012d5d0: the cheapest cell next to this entity (footprint ring or interact
    // nodes) from `goalCell`, its position into `outPos` (action_pf_roots_route.cpp).
    int  FindBestAdjacentCell(uint32_t goalCell, float* outPos) const;
    // 0x14012d230: append this entity to the town's storage-location list
    // (town_storage_seed.cpp). It is StorageLocationComponent's state-2 ENTER handler:
    // returns false (0x14012d25a `XOR AL,AL`), StateMachine's "keep walking" result.
    bool RegisterStorageLocation() const;
    // 0x140107ff0: credit this entity's StorageComponent stock to the town and set its
    // capacity / material flags from its StorageDescription (town_storage_seed.cpp).
    void BuildCompleteStorageInit();

    // ---- the 0x18 bytes Component adds, retail order ------------------------
    // PUBLIC while unconverted bodies read them at raw offsets (decision 1).
    Object::InternalPtr<ComponentEntity> _entity; // +0x10 owning ComponentEntity (InternalPtr<ComponentEntity> in
                              //       retail: Persist nulls it unless IsDerivedFrom ComponentEntity)
    Description* _description; // +0x18 ExternalPtr<Description>: 0x140031d10 IsDerivedFrom 0x1401c72e0 (Description)
    uint32_t _flags;         // +0x20 capability flags (ComponentFlags); the ctor sets Startup
    uint32_t _pad24;         // +0x24 never written by the ctor. Port-only use: ParticleComponent's
                             //       un-stamp frame stamp (UNSTAMP.md); not serialized, not read by retail

protected:
    // 0x1401188b0. The `Internal()` base constructor is the retail
    // `??0Internal@Object@@` call; the vptr stores are the compiler's.
    Component();

    // The retail flag-OR idiom every leaf ctor inlines after its own stamps:
    //   old = _flags; _flags |= bits; if ((old ^ _flags) & 0xffff && _entity)
    //       { InternalBasePtr(&_entity) ; 0x1400c3470(_entity) }
    // In a ctor `_entity` is always null (Component() zeroed it), so only the
    // OR survives. The notify arm is NOT ported here: a leaf that ORs flags
    // outside its ctor ports it with the caller (B38_RULES §6).
    void OrFlagsInCtor(uint32_t bits) { _flags |= bits; }
};

static_assert(sizeof(Component) == 0x28, "Component is 0x28 bytes (0x10 Internal + 0x18)");
static_assert(offsetof(Component, _pad24) == 0x24, "Component::_pad24");
static_assert(offsetof(Component, _entity)      == 0x10, "Component +0x10 entity (0x1401188de)");
static_assert(offsetof(Component, _description) == 0x18, "Component +0x18 (0x1401188e2)");
static_assert(offsetof(Component, _flags)       == 0x20, "Component +0x20 flags = 0x80 (0x1401188e6)");

