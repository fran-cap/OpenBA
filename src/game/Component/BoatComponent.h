#pragma once
// ============================================================================
// src/game/Component/BoatComponent.h — ENDGAME B.3.8 block MI-A
//
// `BoatComponent` as a real C++ class with a compiler-generated vtable and its
// state controller as an EMBEDDED MEMBER (B38_RULES.md D1-D6; there is no
// multiple inheritance). The engine/types POD is a forwarder to this file.
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §2 and §6, bytes read 2026-09-26):
//   reflected "BoatComponent", parent "ControllerComponent", ti 0x1401c4020
//   create 0x1400036e0: MemAlloc(0x98, 8) + ctor 0x140046800
//   createInPlace = the imported `Internal::CreateInPlaceObject` (returns null);
//     the tree's in-place entry builds the object and is kept (B38 B8-13).
//   ctor 0x140046800: Component ctor; +0x28..+0x40 = 0, +0x48 = 0, +0x50 = 0,
//     +0x58 = 0, +0x60 = +0x68 = 0 (route array), +0x70 = -1; member at +0x78.
//     No flag OR (0x80).
//   dtor 0x140046770: member restamp (+0x78, 0x1400467a5), THEN MemFree(+0x60) and
//     +0x60 = +0x68 = 0 (~Array of the route waypoint array, 0x1400467b1..0x1400467c1),
//     ~Internal: reverse declaration order. [0] 0x140003970 = dtor + MemFree.
//   table 0x140158f40, 18 slots:
//     [0] 0x140003970 dtor  [1]/[2] TypeName/Type  [3] 0x1400465d0 Persist
//     [16] 0x140003730 `XOR EAX,EAX`  [17] 0x140003720 `LEA RAX,[RCX+0x78]`
//     every other slot = ControllerComponent's / Component's.
//   member table 0x140158ef0 (9 slots): the table-driven state controller over the
//     class's state table (ControllerLive::Dispatcher<BoatCtrlTag>), stamped at
//     this+0x78 after the interface 0x14015f070; back-pointer at this+0x90.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>
#include "Component/ControllerComponent.h"
#include "collection/Collection.h"   // Collection::Array (route)
#include "Component/controller_state_table.h"
#include "Object/exe_type.h"
#include "math/Math.h"

class ComponentEntity;      // Entity/ComponentEntity.h
class ModelComponent;       // Component/ModelComponent.h
class CharacterComponent;   // Component/CharacterComponent.h

struct BoatCtrlTag { using Owner = class BoatComponent; };   // the handlers' receiver (controller_state_table.h)

class InteractComponent;
namespace System { class String; }
// 0x140123880 Entity::FindAttachPoint (body: boatcomponent_route.cpp): a named attach point ("boat" for a
// trading post's dock) of `comp` or one of its child use locations, in world position and heading.
// False when none carries the name. Also read by ext.winter_ice (the boats' open channel).
extern "C" bool BoatRoute_FindAttachPoint(InteractComponent* comp, const System::String* nameStr, float* outPos,
                                          float* outYaw);

class BoatComponent final : public ControllerComponent {
    DECLARE_EXE_TYPE(BoatComponent)

public:
    using StateController = ::ControllerLive::TableController<BoatCtrlTag>;

    BoatComponent();                                      // 0x140046800

    // 0x140046770
    ~BoatComponent() override;                                    // [0]  0x140003970
    void          Persist(IO::Stream& stream) override;                 // [3]  0x1400465d0
    int         GetDefaultState() const override { return 0; }  // [16] 0x140003730
    Controller* GetController() override { return &_controller; }  // [17] 0x140003720

    // ---- non-virtual members (retail addresses) ----
    // 0x1400456e0: aims the boat at `destination`'s "boat" attach point and builds
    // the route; a null destination releases the boat.
    void SetDestination(const Object::InternalPtr<ComponentEntity>* destination);
    char Steer();                                                 // 0x1400463d0
    // The state handlers of the controller's table (RCX = [node+0x18] = this).
    char State0Enter();                                           // 0x1400453f0
    char State0Update();                                          // 0x140045190
    char State0Tick(StateMachine* sm);                                    // 0x140045250 (trans30)
    char State1Enter();                                           // 0x140045150
    char State1Tick(StateMachine* sm);                                    // 0x140044ff0 (trans30)
    char State2Enter();                                           // 0x140044f70
    char State3Enter();                                           // 0x140044ec0
    char State3Tick(StateMachine* sm);                                    // 0x140044d10 (trans30)
    char State4Enter();                                           // 0x140044cd0
    char State4Tick();                                            // 0x140044c40 (trans30)

    // PUBLIC while the ported bodies read them (B38 decision 1). Types from
    // Persist 0x1400465d0: three InternalBasePtr (import 0x140152aa0) validated
    // against ComponentEntity 0x1401c5d70, ModelComponent 0x1401c7a50 and
    // CharacterComponent 0x1401c7078; Vector3::Serialize (0x140152a68); float
    // (0x140152a60); the route index int and the Vector3 route array.
    Object::InternalPtr<ComponentEntity>    destination;     // +0x28 the trading post the boat sails to
    Object::InternalPtr<ModelComponent>     model;           // +0x30 the boat's own ModelComponent (State0Enter)
    Object::InternalPtr<CharacterComponent> character;       // +0x38 the boat's own CharacterComponent (State0Enter)
    Math::Vector3       dockPosition;    // +0x40 the destination's "boat" attach point
    float               dockHeading;     // +0x4c its heading
    // +0x50..+0x73 is one sub-object retail hands to 0x1400a70c0 / 0x1400a7310 (also
    // used by TornadoComponent): the current waypoint, the route array, the index.
    Math::Vector3       waypoint;        // +0x50 the current waypoint
    uint8_t             _pad5C[4];       // +0x5c
    // +0x60 the route (count +0x68, capacity +0x6c). Math::Vector3's trait-derived row is
    // exe 0x14000d030's: stride 0xc, align 4, the new tail zeroed (Vector3() 0x180015070),
    // no element destroy.
    Collection::Array<Math::Vector3> route;
    int32_t             routeIndex;      // +0x70 the waypoint's index (retail ctor: -1)
    uint8_t             _pad74[4];       // +0x74
    StateController _controller;   // +0x78 the embedded controller (D6)
};

static_assert(sizeof(BoatComponent) == 0x98, "BoatComponent is 0x98 (create 0x1400036e0)");
static_assert(offsetof(BoatComponent, _pad5C) == 0x5C, "BoatComponent::_pad5C");
static_assert(offsetof(BoatComponent, _pad74) == 0x74, "BoatComponent::_pad74");
static_assert(alignof(BoatComponent) == 8, "BoatComponent alignment");
static_assert(offsetof(BoatComponent, _controller) == 0x78,
              "BoatComponent's controller member at +0x78 (slot 17 0x140003720: LEA RAX,[RCX+0x78])");
static_assert(offsetof(BoatComponent, _controller) + offsetof(ControllerComponent::Controller, _owner) == 0x90,
              "controller back-pointer at +0x90 (ctor 0x140046800)");
static_assert(offsetof(BoatComponent, destination) == 0x28, "BoatComponent::destination @ +0x28");
static_assert(offsetof(BoatComponent, model) == 0x30, "BoatComponent::model @ +0x30");
static_assert(offsetof(BoatComponent, character) == 0x38, "BoatComponent::character @ +0x38");
static_assert(offsetof(BoatComponent, dockPosition) == 0x40, "BoatComponent::dockPosition @ +0x40");
static_assert(offsetof(BoatComponent, dockHeading) == 0x4c, "BoatComponent::dockHeading @ +0x4c");
static_assert(offsetof(BoatComponent, waypoint) == 0x50, "BoatComponent::waypoint @ +0x50");
static_assert(offsetof(BoatComponent, route) == 0x60, "BoatComponent::route @ +0x60");
static_assert(offsetof(BoatComponent, route) + offsetof(Collection::Array<Math::Vector3>, _size) == 0x68, "BoatComponent::route count @ +0x68");
static_assert(offsetof(BoatComponent, route) + offsetof(Collection::Array<Math::Vector3>, _capacity) == 0x6c, "BoatComponent::route capacity @ +0x6c");
static_assert(offsetof(BoatComponent, routeIndex) == 0x70, "BoatComponent::routeIndex @ +0x70");
static_assert(std::is_trivially_copyable<Math::Vector3>::value && std::is_trivially_destructible<Math::Vector3>::value &&
              !std::is_trivially_default_constructible<Math::Vector3>::value,
              "route's trait row: byte copy, no destroy, a zeroing init (exe 0x14000d030)");
