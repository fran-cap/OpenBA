// src/game/Component/citizenaicomponent_serialize.cpp
// Slot-3 (+0x18) stream Serialize for CitizenAIComponent.
//
//   CitizenAIComponent::Serialize   retail 0x14010f960   object size 0xd0
//
// Save/load wave (2026-08-27): citizen entities crash on the first post-load
// tick because CitizenAIComponent's stream-Serialize vtable slot was only the
// Component-base default — the AI state (+0x28), sibling-citizen backref
// (+0x38) and the 16 per-state Time timers (+0x40..+0xc0/+0xc8) never
// round-tripped. Wired unconditionally in citizenaicomponent.cpp.
//
// Retail body (byte-faithful):
//   Component_Serialize(self, stream);
//   Serializer ser(stream, /*version*/5);
//   IBP::Serialize(self+0x38, ser); validate sibling CitizenComponent, null on mismatch.
//   Serialize_Int(ser, self+0x2c);                       // pending-state
//   if (ser.GetVersion() < 5) {
//       for 14 Time entries at self+0x40 (stride 8): Time::Serialize(entry, ser);
//       self+0xb0 = Time(0.0f);   self+0xb8 = Time(0.0f);  // v<5 stream lacked these
//   } else {
//       for 16 Time entries at self+0x40 (stride 8): Time::Serialize(entry, ser);
//   }
//   if (ser.GetVersion() > 0) { Serialize_Bool(self+0x30); Serialize_Bool(self+0x31); }
//   if (ser.GetVersion() > 1) { Serialize_Int(self+0x28); }         // aiState
//   if (ser.GetVersion() > 2) { Time::Serialize(self+0xc0, ser); }
//   if (ser.GetVersion() > 3) { Time::Serialize(self+0xc8, ser); }
//
// Sub-callees: all resolved through the shared engine_funcs.h globals already
// initialized at startup (g_fn_Serializer_Ctor/Dtor/GetVersion, g_fn_Serialize_Int,
// g_fn_Serialize_Bool, g_fn_Time_Serialize, fn_Time_Ctor_Float, fn_InternalBasePtr_Serialize,
// fn_IsDerivedFrom) plus the already-ported Component base serialize
// (Component::Persist, component.cpp — called qualified).
// No new sub-function ports were required.
//
// Validation type for +0x38 (sibling CitizenComponent) is CitizenComponent's own
// type id, 0x1401c7158 — GRADUATED default-on (see project_citid_graduated memory),
// so we resolve it the same self-contained way citizenaicomponent.cpp already does
// for its own sibling-find (GetTypeIdByName, not the raw .rdata global).

#include "object/TypeInfo.h"
#include "Object/factory_registration.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.4 caller batch G
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/CitizenComponent.h"   // ENDGAME B.3.8 CIT (TypeId)
#include "Component/CitizenAIComponent.h"


namespace {

uint32_t CitizenAISer_TidCitizenComponent() {
    // B.3.8 CIT (B6-5): the class's own record, no lazy first-use cache.
    return CitizenComponent::TypeId();
}

} // namespace

void CitizenAIComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail's direct CALL 0x1401189b0

    alignas(8) uint8_t ser[24];
    IO::Serializer* s = ::new (static_cast<void*>(ser)) IO::Serializer(stream, 5);

    // citizen is an InternalPtr<CitizenComponent> (eight bytes): its serializer.
    citizen.Object::InternalBasePtr::Serialize(*s);
    if (citizen) {
        uint32_t tid = CitizenAISer_TidCitizenComponent();
        // NOT RETAIL (F-143): the type test runs on the held Internal*, before any
        // downcast (`citizen->` was one); the answer is the same.
        if (tid && !::Object::IsDerivedFromNullSafe(citizen, tid))
            citizen = nullptr;
    }

    s->Serialize(reinterpret_cast<int32_t&>(queuedState), nullptr);

    int version = s->GetVersion();
    if (version < 5) {
        for (int i = 0; i < 14; ++i)
            timers[i].Serialize(*s);
        RtSystem_Time_Ctor(&timers[static_cast<int32_t>(CitizenAIState::EatUrgent)], 0.0f);
        RtSystem_Time_Ctor(&timers[static_cast<int32_t>(CitizenAIState::WarmUrgent)], 0.0f);
    } else {
        for (int i = 0; i < 16; ++i)
            timers[i].Serialize(*s);
    }

    version = s->GetVersion();
    if (version > 0) {
        s->Serialize(seekingFood, nullptr);
        s->Serialize(seekingWarmth, nullptr);
    }
    version = s->GetVersion();
    if (version > 1) s->Serialize(reinterpret_cast<int32_t&>(state), nullptr);
    version = s->GetVersion();
    if (version > 2) leisureTimer.Serialize(*s);   // System::Time (X-G1-1b)
    version = s->GetVersion();
    if (version > 3) dataC8.Serialize(*s);

    s->~Serializer();
}
