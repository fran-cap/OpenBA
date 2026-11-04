// ============================================================================
// tests/unit/statusbar_icon_race_test.cpp -- review S-006 (StatusBarUI icon click, 0x14007a120)
// ============================================================================
// The status-bar icon click is queued in frame N (DialogInstance::Update pushes it into
// UserInterface::_events, using the icon flags that frame N's layout pass set) and dispatched in
// frame N+1 by UserInterface::UpdateEvents, which runs at the top of UserInterface::Update. In
// frame N+1, GameUpdateLoop::Update runs the simulation (CitizenBehavior / EntityList) before
// UserInterface::Update. If the last citizen leaves the town in that tick
// (CitizenComponent::OnRemove -> citizens.Remove, ArrayRemove8: _size 1 -> 0, slot 0 not
// cleared), StatusBarUI::OnElementEvent (0x14007a3a0) runs with citizens._size == 0.
//
// The test builds that post-removal state with the real Array::Remove and dispatches the click
// through the real StatusBarUI::OnElementEvent. Expected: with no citizen in the town the click
// does nothing. Observed (retail shape): the handler wraps the index to 0, reads
// citizens._data[0] past _size, and centres the camera on the removed citizen's entity. In
// retail the removed entity is freed by the same frame's EntityList drain, so that read is a
// use-after-free. fix.statusbar_empty_town (on by default) makes the click a no-op; with the row
// off the handler keeps retail's shape.
// ============================================================================
#include "unit.h"
#include "ext/registry.h"
#include "Dialog/StatusBarUI.h"
#include "Game/GameUpdateLoop.h"
#include "Game/PlayerToolManager.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Component/CitizenComponent.h"
#include "Component/StatusIconComponent.h"
#include "object/TypeInfo.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

namespace {

int g_centerCalls = 0;
ComponentEntity* g_centerEntity = nullptr;

// Every slot of the fake component's table answers StatusIconComponent's type id: FindComponent
// calls Type() (slot 2) and IsDerivedFrom, which calls Type() again and answers true for an
// exact match without a lookup (Factory::IsDerivedFrom, type_reflection.cpp).
uint32_t FakeType(const void*) {
    return StatusIconComponent::TypeId();
}

struct FakeTown {
    void* table[64];
    alignas(16) uint8_t icons[sizeof(StatusIconComponent)];
    Object::InternalPtr<Component> components[1];
    alignas(16) uint8_t entity[sizeof(ComponentEntity)];
    alignas(16) uint8_t citizen[sizeof(CitizenComponent)];
    Object::InternalPtr<CitizenComponent> citizenSlots[4];
    alignas(16) uint8_t town[sizeof(CitizenBehavior)];
    alignas(16) uint8_t loop[sizeof(GameUpdateLoop)];
    alignas(16) uint8_t tools[sizeof(PlayerToolManager)];
    Object::Factory factory;

    ComponentEntity* Entity() { return reinterpret_cast<ComponentEntity*>(entity); }
    CitizenComponent* Citizen() { return reinterpret_cast<CitizenComponent*>(citizen); }
    CitizenBehavior* Town() { return reinterpret_cast<CitizenBehavior*>(town); }

    // One citizen whose StatusIconComponent carries `mask`.
    explicit FakeTown(uint32_t mask) {
        std::memset(icons, 0, sizeof(icons));
        std::memset(entity, 0, sizeof(entity));
        std::memset(citizen, 0, sizeof(citizen));
        std::memset(static_cast<void*>(citizenSlots), 0, sizeof(citizenSlots));
        std::memset(town, 0, sizeof(town));
        std::memset(loop, 0, sizeof(loop));
        std::memset(tools, 0, sizeof(tools));
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&FakeType);
        *reinterpret_cast<void***>(icons) = table;
        reinterpret_cast<StatusIconComponent*>(icons)->_geometry.activeMask = mask;
        std::memset(static_cast<void*>(components), 0, sizeof(components));
        *reinterpret_cast<Component**>(&components[0]) = reinterpret_cast<Component*>(icons);
        Entity()->_components._data = components;
        Entity()->_components._size = 1;
        Entity()->_components._capacity = 1;
        *reinterpret_cast<ComponentEntity**>(&Citizen()->_entity) = Entity();
        *reinterpret_cast<CitizenComponent**>(&citizenSlots[0]) = Citizen();
        Town()->citizens._data = citizenSlots;
        Town()->citizens._size = 1;
        Town()->citizens._capacity = 4;
        GameUpdateLoop* game = reinterpret_cast<GameUpdateLoop*>(loop);
        game->m_gameFlags = 0;
        game->m_citizenList = Town();
        game->m_toolManager = reinterpret_cast<PlayerToolManager*>(tools);
        GameUpdateLoop::_singleton = game;
        Object::Factory::_singleton = &factory;
        g_centerCalls = 0;
        g_centerEntity = nullptr;
    }
    ~FakeTown() {
        GameUpdateLoop::_singleton = nullptr;
        Object::Factory::_singleton = nullptr;
        Town()->citizens._data = nullptr;
        Town()->citizens._size = 0;
        Town()->citizens._capacity = 0;
    }
};

// The controller without its DialogController ctor: the handler reads only
// statusCitizenIndex (+0x100) and _dialog.
struct FakeBar {
    alignas(16) uint8_t bytes[sizeof(StatusBarUI)];
    FakeBar() { std::memset(bytes, 0, sizeof(bytes)); }
    StatusBarUI* Get() { return reinterpret_cast<StatusBarUI*>(bytes); }
};

void Click(StatusBarUI* bar, int32_t elementId) {
    // StatusBarUI::OnElementEvent is slot 9 of the real class; it is called directly here
    // (the fake object has no table).
    bar->StatusBarUI::OnElementEvent(nullptr, 2, elementId, 0, 0, 0, nullptr);
}

}  // namespace

// Stand-in for toolbardialog.cpp's body (not linked into the unit build): records the target.
void PlayerToolManager::CenterOnEntity(UI::DialogInstance* const*, ComponentEntity* const* entity) {
    ++g_centerCalls;
    g_centerEntity = *entity;
}

// Control: with the citizen in the town, a click on the food icon (id 101, bit 1) centres on it.
UNIT_TEST(statusbar_icon_click_finds_live_citizen) {
    FakeTown town(0x2u);
    FakeBar bar;
    Click(bar.Get(), 101);
    CHECK_EQ(g_centerCalls, 1);
    CHECK(g_centerEntity == town.Entity());
}

// S-006: the click was queued while the citizen was in the town; the sim tick of the next
// frame removed the last citizen before UpdateEvents dispatched it.
UNIT_TEST(statusbar_icon_click_after_last_citizen_removed) {
    FakeTown town(0x2u);
    FakeBar bar;
    CitizenComponent* last = town.Citizen();
    town.Town()->citizens.Remove(Object::InternalPtr<CitizenComponent>(last));   // ArrayRemove8
    CHECK_EQ(town.Town()->citizens._size, 0);
    // Slot 0 still holds the removed citizen: ArrayRemove8 never clears the vacated slot.
    CHECK(*reinterpret_cast<CitizenComponent**>(&town.citizenSlots[0]) == last);

    Click(bar.Get(), 101);
    // An empty town has no citizen to centre on.
    CHECK_EQ(g_centerCalls, 0);
    CHECK(g_centerEntity == nullptr);
    if (g_centerEntity == town.Entity())
        std::printf("  S-006: the click centred on the removed citizen's entity (_data[0] read with _size == 0)\n");
}

// The row off: retail's shape, the walk wraps to slot 0 and centres on the removed citizen.
UNIT_TEST(statusbar_icon_click_after_last_citizen_removed_row_off_is_retail) {
    Ext::State off;
    off.fix.statusbar_empty_town = false;
    Ext::Install(off, Ext::Profile::PerEntry, false);
    FakeTown town(0x2u);
    FakeBar bar;
    CitizenComponent* last = town.Citizen();
    town.Town()->citizens.Remove(Object::InternalPtr<CitizenComponent>(last));   // ArrayRemove8
    Click(bar.Get(), 101);
    CHECK_EQ(g_centerCalls, 1);
    CHECK(g_centerEntity == town.Entity());
    Ext::Install(Ext::State(), Ext::Profile::PerEntry, false);
}
