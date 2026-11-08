// src/game/Game/scenario.cpp
// Scenario (0x90 bytes) -- game scenario definition.
// Extends Achievement::Achievement (0x58 base).
// ENDGAME B.3.8 block GOAL: a REAL class (Game/Scenario.h) with a
// compiler-generated vtable; the retail evidence per slot is in the header.
//
// Holds references to MapGenerator, TerrainGenerator, ClimateGenerator,
// and scenario name/description strings. The Achievement base provides
// goal lists and achievement tracking.
//
// Decompiled from Application-x64.exe.

#include "Game/Scenario.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "object/Internal.h"      // Object::detail::InternalMemAlloc / InternalMemFree
#include "Object/factory_registration.h"
#include "system/String.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch B
#include "achievement/AchievementManager.h"   // ENDGAME B.3.7 callers (K2): QUALIFIED base calls
#include <cstdint>
#include <new>
#include "Game/MapGenerator.h"   // B8 RT2: ::Object::SerializeExternal<MapGenerator> (the downcast needs it complete)
#include "Game/ClimateGenerator.h"   // B8 RT2: ::Object::SerializeExternal<ClimateGenerator> (the downcast needs it complete)
#include "Game/TerrainGenerator.h"   // B8 RT2: ::Object::SerializeExternal<TerrainGenerator> (the downcast needs it complete)

// The three typed ExternalPtr serializers (exe 0x140019ec0 / 0x14001a0c0 /
// 0x1400194a0), ported in serialize_externals_typed.cpp and registered at
// those addresses by address_registry.cpp. ENDGAME B.3.8 (B6-5): called
// directly instead of through a lazily filled ResolveAddr cache (the menu
// loader thread builds Scenarios).

// ============================================================
// Construction / destruction
// ============================================================
// ctor 0x1400a9480: Achievement() (0x18009e240, the base's real ctor), the
// vptr, then the Scenario fields:
//   *(uint64*)(+0x58)=0, *(uint64*)(+0x60)=0, *(uint16*)(+0x68)=1,
//   *(uint64*)(+0x70)=0, *(uint64*)(+0x78)=0, *(uint64*)(+0x80)=0, *(uint64*)(+0x88)=0
Scenario::Scenario()
    : randomSeed(0), terrainSize(0), disasters(true), tutorial(false),
      _pad6A{}, mapGenerator(nullptr), terrainGenerator(nullptr), climateGenerator(nullptr),
      saveGame() {}

// The dtor inside [0] 0x14001eb40: the two Strings (+0x88 then +0x58), the
// compiler's member destruction; the compiler then runs ~Achievement
// (0x18009e180, retail's CALL).
Scenario::~Scenario() {}

// ============================================================
// Scenario::GetFileVersion (vtable +0x18, 0x1400a9460)
// ============================================================
// Achievement's GetFileVersion OR'd with 0x600 (Scenario's own version bits).
int32_t Scenario::GetFileVersion() const {
    // QUALIFIED: an unqualified call would dispatch back into this slot.
    uint32_t baseVersion = (uint32_t)::Achievement::Achievement::GetFileVersion();
    return static_cast<int32_t>(baseVersion | 0x600);
}

// ============================================================
// Scenario::Serialize (vtable +0x28, 0x1400a9370)
// ============================================================
// First the Achievement base, then:
//   int +0x60, int +0x64, String +0x58, ExtPtr<MapGen> +0x70,
//   ExtPtr<TerrainGen> +0x78, ExtPtr<ClimateGen> +0x80,
//   String +0x88, bool +0x68, bool +0x69
bool Scenario::Serialize(IO::Stream& stream) {
    // Achievement::Achievement::Serialize (slot 5, 0x18009e070) — QUALIFIED (B7-3).
    ::Achievement::Achievement::Serialize(stream);

    // The serializer, version 0
    IO::Serializer serializer(stream, 0);

    // Serialize Scenario-specific fields
    serializer.Serialize(randomSeed, nullptr);
    serializer.Serialize(terrainSize, nullptr);

    scenarioName.Serialize(serializer);

    ::Object::SerializeExternal<MapGenerator>(serializer, mapGenerator);
    ::Object::SerializeExternal<TerrainGenerator>(serializer, terrainGenerator);
    ::Object::SerializeExternal<ClimateGenerator>(serializer, climateGenerator);

    saveGame.Serialize(serializer);

    serializer.Serialize(disasters, nullptr);
    serializer.Serialize(tutorial, nullptr);

    return true;
}

// ============================================================
// Scenario::Reconstruct [6] 0x14001ead0 / Destruct [7] 0x140001640
// ============================================================
// `this->~T()` (retail: slot 0, flags 0 -- destroy in place), then the ctor on `this`.
Object::External* Scenario::Reconstruct() {
    this->~Scenario();
    return ::new (static_cast<void*>(this)) Scenario();
}
void Scenario::Destruct() { this->~Scenario(); }

// ============================================================
// Scenario::Construct (vtable +0x40, 0x14001ea70)
// ============================================================
// Placement-new on `this` (retail folded it with createInPlace).
Object::External* Scenario::Construct() {
    return ::new (static_cast<void*>(this)) Scenario();
}

// create 0x14001e9f0: MemAlloc(0x90, 8), then the (inlined) ctor. The menu
// loader thread builds one when External::Load reads a Scenario out of the
// Game/Global resource tree.
::Object::Base* Scenario::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(Scenario), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) Scenario());
}
// createInPlace 0x14001ea70.
::Object::Base* Scenario::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(::new (storage) Scenario());
}


// Parent string matches the original Scenario_RegisterType (0x14014cd10)
// exactly, leading "::" included; Factory::Startup resolves the parent by
// literal name against registered types.
DEFINE_EXE_TYPE(Scenario, "::Achievement::Achievement")
