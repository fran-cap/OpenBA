#pragma once
// ============================================================================
// src/game/Component/ResidenceComponent.h — ENDGAME B.3.8 block MI-A
//
// `ResidenceComponent` as a real C++ class with a compiler-generated vtable and its
// state controller as an EMBEDDED MEMBER (B38_RULES.md D1-D6; there is no
// multiple inheritance). The engine/types POD is a forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §2 and §6, bytes read 2026-09-26):
//   reflected "ResidenceComponent", parent "ControllerComponent", ti 0x1401c7cf0
//   create 0x140037440: MemAlloc(0x60, 8) + ctor 0x14012a2a0
//   createInPlace = the imported `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and is kept (B38 B8-13).
//   ctor 0x14012a2a0: Component ctor; member at +0x28; +0x48 = +0x50 = +0x58
//     = 0; `_flags |= 0x82`.
//   dtor 0x14012a230 (inlined in [0] 0x140037610): 0x1400175d0 on +0x50 (free
//     each resident slot's inner array, then the slot array; zero +0x50/+0x58),
//     member restamp, ~Internal.
//   table 0x1401642c0, 18 slots:
//     [0] 0x140037610 dtor  [1]/[2] TypeName/Type  [3] 0x14012a170 Persist
//     [4] 0x14012a120 Startup (size the resident array)  [7] 0x14012a0e0 OnRemove
//     [16] 0x1400026c0 `MOV EAX,2`  [17] 0x140004080 `LEA RAX,[RCX+0x28]`
//     every other slot = ControllerComponent's / Component's.
//   member table 0x140164270 (9 slots): the table-driven state controller over the
//     class's state table (ControllerLive::Dispatcher<ResidenceCtrlTag>), stamped at
//     this+0x28 after the interface 0x14015f070; back-pointer at this+0x40.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/ControllerComponent.h"
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"
#include "system/Value.h"   // System::Time
#include "object/Internal.h"   // Object::InternalPtr<T>
#include "collection/Collection.h"   // Collection::Array (the families)

class CitizenComponent;
namespace rtcoll { extern const ArraySpec kSpec_PtrZero; }   // 0x180008d60 (Entity/EntityList.h)
namespace IO { class Serializer; }

struct ResidenceCtrlTag { using Owner = class ResidenceComponent; };   // the handlers' receiver (controller_state_table.h)

class ResidenceComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(ResidenceComponent)

public:
    using StateController = ::ControllerLive::TableController<ResidenceCtrlTag>;

    // One family (household slot): Collection::Array<InternalPtr<CitizenComponent>>,
    // 0x10 bytes (0x140037c50 serializes each element as an InternalBasePtr checked
    // against CitizenComponent's type; 0x140007960 copies it; 0x14012a4f0 compares the
    // count against ResidenceDescription::_maxResidentsPerFamily). Retail's stride-8 row
    // (kSpec_PtrZero: no element destroy); ~Family is the MemFree + {data, size + capacity}
    // zero stores 0x1400175d0 runs per element.
    using Family = Collection::Array<Object::InternalPtr<CitizenComponent>, Collection::DefaultAllocator,
                                     Collection::ExplicitSpec<&rtcoll::kSpec_PtrZero>>;
    // Array<Family>: resized by the hand-written SetFamilyCount 0x140037ad0 (CopyFamily
    // 0x140007960 per element); ~FamilyArray is 0x1400175d0 (each family's ~Array, then
    // MemFree of the buffer and the header zeroed).
    using FamilyArray = Collection::Array<Family>;

    ResidenceComponent();                                 // 0x14012a2a0

    // 0x14012a230
    ~ResidenceComponent() override;                               // [0]  0x140037610
    void          Persist(IO::Stream& stream) override;                 // [3]  0x14012a170
    void          Startup() override;                             // [4]  0x14012a120
    void          OnRemove() override;                            // [7]  0x14012a0e0
    int         GetDefaultState() const override { return SmId(BuildingState::Built); }  // [16] 0x1400026c0
    Controller* GetController() override { return &_controller; }  // [17] 0x140004080

    // The family roster (non-virtual members; receiver RCX = this, fields +0x50/+0x58).
    bool     IsFamilyFull(int family) const;                          // 0x14012a4f0
    void     RemoveResident(int family, Object::InternalPtr<CitizenComponent>* citizen);  // 0x14012a550
    void     AddResident(int family, Object::InternalPtr<CitizenComponent>* citizen);     // 0x14012a710
    uint32_t CountResidents() const;                                  // 0x140129ff0
    void     EvictResidents();                                        // 0x140129ef0
    static void SerializeFamilies(IO::Serializer& ser, int count, Family* families);  // 0x140037c50
    void SetFamilyCount(int newCount);                     // 0x140037ad0 Array<Family>::SetSize on residentSlots

    // State handlers (the controller's table, 0x14012a350).
    char     EnterBuilt();                                            // 0x140129de0 state Built enter
    char     EnterDemolish();                                         // 0x140129920 state Demolish enter
    char     HeatTick(const System::Time* dt);                            // 0x140129970 state Built per-frame (residence_heattick.cpp)
    bool     CheckHeating();                                          // 0x14012ac30 desc +0x28 <= the weather value, or fuel time left (+0x48 > 0)
    // The owner's StorageComponent units (count - reserved) of one material class; RCX
    // is the residence (HeatTick 0x140129aab MOV RCX,RSI; the Gather states hand the
    // 0x140008090 result, 0x14010bf2f).
    int      GetFoodCount() const;                                    // 0x14012a9d0 RawMaterialDescription::_flags bit 5
    int      GetFuelCount() const;                                    // 0x14012ab00 RawMaterialDescription::_flags bit 6

    // PUBLIC while the ported bodies read them at raw offsets (B38 decision 1).
    StateController _controller;   // +0x28 the embedded controller (D6)
    System::Time    heatRemaining; // +0x48 countdown (heat tick 0x140129970; Persist Time::Serialize)
    FamilyArray     residentSlots; // +0x50 Array<Family> (count +0x58 = ResidenceDescription::_maxFamilies,
                                   //       capacity +0x5c; SetSize 0x140037ad0; dtor 0x1400175d0)

private:
    void     EvictAndUnregister();   // the body shared by [7] 0x14012a0e0 and 0x140129920
};

static_assert(sizeof(ResidenceComponent) == 0x60, "ResidenceComponent is 0x60 (create 0x140037440)");
static_assert(alignof(ResidenceComponent) == 8, "ResidenceComponent alignment");
static_assert(offsetof(ResidenceComponent, _controller) == 0x28,
              "ResidenceComponent's controller member at +0x28 (slot 17 0x140004080: LEA RAX,[RCX+0x28])");
static_assert(offsetof(ResidenceComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x40,
              "controller back-pointer at +0x40 (ctor 0x14012a2a0)");
static_assert(offsetof(ResidenceComponent, heatRemaining) == 0x48, "ResidenceComponent +0x48");
static_assert(offsetof(ResidenceComponent, residentSlots) == 0x50, "ResidenceComponent +0x50");
static_assert(offsetof(ResidenceComponent, residentSlots) + offsetof(ResidenceComponent::FamilyArray, _size) == 0x58, "ResidenceComponent +0x58");
static_assert(offsetof(ResidenceComponent, residentSlots) + offsetof(ResidenceComponent::FamilyArray, _capacity) == 0x5C, "ResidenceComponent +0x5c (SetSize 0x140037ad0)");
static_assert(sizeof(ResidenceComponent::Family) == 0x10, "a family is 0x10 bytes (0x140037ad0 stride)");
static_assert(offsetof(ResidenceComponent::Family, _size) == 0x08, "family count +0x08 (0x14012a53c)");
static_assert(offsetof(ResidenceComponent::Family, _capacity) == 0x0C, "family capacity +0x0C (0x140007960)");
