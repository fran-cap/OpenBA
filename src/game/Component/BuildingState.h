#pragma once
// src/game/Component/BuildingState.h
// The state ids and event ids of the building / field controller chain: the ids a
// building entity's StateMachineComponent requests and runs (StateMachine::currentInput
// +0x08 / pendingState +0x0a), that every ControllerComponent leaf on the chain registers
// in its state table (StateEntry::id, TransEntry::targetId) and answers from
// GetDefaultState, and the event ids posted through node slot 2 (PostEvent) and matched
// against EventEntry::eventId.
//
// The values are retail's (the TableRegisterState / TableAddTransition / TableAddEvent
// populators of every controller and their callers); the enums only name them. The id
// space is SHARED by every controller of a building entity, so it lives here rather than
// in one component's header.
//
// Boat (BoatComponent, 0..4) and Nomad (NomadComponent, 0..2) tables run on other entity
// kinds and reuse the low ids with other meanings: they have their own enums. That is
// why StateMachine::currentInput / pendingState stay raw int8_t fields. activeState /
// transitionState are table INDICES, not ids, and never take a state enum.

#include <cstdint>
#include <type_traits>

// A building-chain state id. Stored as a signed byte (StateMachine +0x08 / +0x0a,
// serialized as signed char); the tables widen it to int.
enum class BuildingState : int8_t {
    None                = -1,   // no state / no pending request (0xff)
    ClearSite           = 0,    // ClearArea / WorkPlace / AbsorbResource: clear the footprint
    GatherMaterials     = 1,    // BuildComponent default: haul the build materials
    Built               = 2,    // the finished building's idle/working state
    Demolish            = 3,    // DestroyComponent: demolition requested (also fire "begin")
    HaulAway            = 4,    // DestroyComponent: carry the salvage away
    Deconstruct         = 5,    // DestroyComponent: take the building apart
    Collapse            = 6,    // DestroyComponent: collapse into rubble
    Rubble              = 7,    // DestroyComponent: rubble (also fire "stop")
    Construct           = 8,    // BuildComponent: the builders raise it
    Plant               = 9,    // CropField / Orchard
    Tend                = 10,   // CropField / Orchard
    Harvest             = 11,   // CropField / Orchard
    Replant             = 12,   // Orchard
    Producing           = 13,   // ResourceProduce / Happiness
    Paused              = 14,   // ResourceProduce
    ImmigrationWaiting  = 15,   // ImmigrationComponent (0xf)
    ImmigrationArriving = 16,   // ImmigrationComponent (0x10)
    ImmigrationAnswer   = 17,   // ImmigrationComponent (0x11)
    MerchantInbound     = 18,   // TradeComponent (0x12)
    MerchantDocked      = 19,   // TradeComponent (0x13)
    OrchardClear        = 20,   // Orchard (0x14)
    Reclear             = 21,   // ClearArea (0x15)
};
static_assert(sizeof(BuildingState) == 1, "BuildingState is the StateMachine state byte");

// A building-chain event id: the eventId of TableAddEvent and of Controller::PostEvent
// (node slot 2, int). Id 0 is the "cancel the current state" event every table that
// reacts to it registers under its own name.
enum class BuildingEvent : int32_t {
    Cancel            = 0,    // DestroyCancel (states 4/5); destroyui cancel
    Rebuild           = 0,    // DestroyState7Rebuild (state 7 Rubble)
    TogglePause       = 0,    // RP_Event3Pause / RP_Event3Resume (states 13/14)
    Remove            = 1,    // DestroyState7Remove; destroyui remove; removal path
    ToRubble          = 2,    // Destroy state 6 -> rubble
    Plant             = 3,    // CropField / Orchard panel command
    Harvest           = 4,    // CropField / Orchard panel command
    Clear             = 5,    // Orchard panel command
    ImmigrantsArrived = 6,    // ImmOnArrived
    AllowImmigrants   = 7,    // ImmOnAllow
    DenyImmigrants    = 8,    // ImmOnDeny
    MerchantDocked    = 9,    // TradeCtrl_Event09_MerchantDocked
    MerchantDismissed = 10,   // TradeCtrl_Event0A_Dismissed (tradeui)
    MerchantLost      = 11,   // TradeCtrl_Event0B_MerchantLost
};
static_assert(sizeof(BuildingEvent) == 4, "BuildingEvent is the int event id");

// ── Conversions. Plain static_casts (an earlier cleanup found a helper on a variable can
//    change GCC codegen; these are constexpr and used on constants or whole expressions).
//    Generic over any state / event enum, so the Boat and Nomad enums reuse them. ──────
template <class E, typename std::enable_if<std::is_enum<E>::value, int>::type = 0>
constexpr int8_t StateByte(E e) {
    return static_cast<int8_t>(e);
}
template <class E, typename std::enable_if<std::is_enum<E>::value, int>::type = 0>
constexpr int SmId(E e) {
    return static_cast<int>(e);
}
constexpr BuildingState AsBuildingState(int8_t raw) {
    return static_cast<BuildingState>(raw);
}
