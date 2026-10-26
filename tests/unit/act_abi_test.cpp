// ============================================================================
// tests/unit/act_abi_test.cpp — ENDGAME B.3.8 block ACT (B38_RULES.md B8-14)
//
// The ABI claims of the Action family, measured on the SHIPPED HEADERS
// (src/game/Action/*.h):
//   PART A — layout: sizeof / alignof / field offsets are the headers' own
//            static_asserts (src/game/Action/*.h); only what no header pins is here.
//   PART B — slot order, read off pointers to VIRTUAL members (Itanium PMF =
//            1 + vtable byte offset; adjustment 0 = single inheritance):
//            retail table 0x14015faa0, 10 slots, and each leaf's overrides at
//            retail's index (B38_SLOTS.md §6).
//   PART C — class-shape rules: polymorphic, a virtual destructor (B3X W1: retail slot 0 = D1 + D0), Action
//            abstract (slot 4 `_purecall`), every leaf concrete.
//
// NOT PROVEN HERE: anything needing a constructed object, i.e. the member bodies,
// which stand on the address registry and do not link into the host test.
// ============================================================================
#include "unit.h"
#include "slot_check.h"

#include "Action/Action.h"
#include "Action/AnimateNaturalResource.h"
#include "Action/CompleteRoad.h"
#include "Action/CompleteTask.h"
#include "Action/ConsumeRawMaterial.h"
#include "Action/CreateNaturalResource.h"
#include "Action/CreateRawMaterial.h"
#include "Action/DropOffRawMaterial.h"
#include "Action/HuntAnimal.h"
#include "Action/MoveTo.h"
#include "Action/PerformWork.h"
#include "Action/PickupRawMaterial.h"
#include "Action/PickupStoredRawMaterial.h"
#include "Action/PlayAnimation.h"
#include "Action/RemoveAnimal.h"
#include "Action/RemoveNaturalResource.h"
#include "Action/SendComponentMessage.h"
#include "Action/ShowTool.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>

namespace {

// ===========================================================================
// Action (the base, retail 0x14015faa0)
// ===========================================================================
UNIT_TEST(act_action_layout) {
    // sizeof / alignof / the field offsets are Action.h's static_asserts.
    CHECK_EQ(sizeof(Action::_next), (size_t)8);
}

UNIT_TEST(act_action_shape) {
    CHECK(std::is_polymorphic<Action>::value);
    CHECK(std::is_abstract<Action>::value);                 // [4] _purecall 0x14003addc
    CHECK(std::has_virtual_destructor<Action>::value);
    CHECK((std::is_base_of<Object::Internal, Action>::value));
    CHECK(std::is_trivially_destructible<Object::InternalBasePtr>::value);
}

UNIT_TEST(act_action_slot_order) {
    CHECK_DTOR_SLOT(Action);   // 0x140021c90
    CHECK_SLOT(Action, TypeName,     0x08/8);   // 0x140021c80
    CHECK_SLOT(Action, Type,         0x10/8);   // 0x140021c70
    CHECK_SLOT(Action, Persist,      0x18/8);   // 0x1400c4b40
    CHECK_SLOT(Action, AbstractImpl, 0x20/8);   // _purecall
    CHECK_SLOT(Action, Execute,      0x28/8);   // 0x1400c4a90
    CHECK_SLOT(Action, Enter,        0x30/8);   // 0x1400c4b90
    CHECK_SLOT(Action, Exit,         0x38/8);   // 0x1400c4ba0
    CHECK_SLOT(Action, CheckEntity,  0x40/8);   // 0x1400c4a30
    CHECK_SLOT(Action, GetTarget,    0x48/8);   // 0x1400c4a40 (last)
}

// ===========================================================================
// The leaves: every one is concrete, final-shape, 8-aligned, retail-sized, and
// its overrides sit on retail's slot index (B38_SLOTS.md §6).
// ===========================================================================
// sizeof and every field offset are each leaf header's static_asserts; alignof is
// asserted only for Action itself (Action.h), so the leaves are checked here.
template <typename T>
void CheckLeafShape() {
    CHECK_EQ(alignof(T), (size_t)8);
    CHECK(std::is_polymorphic<T>::value);
    CHECK(!std::is_abstract<T>::value);
    CHECK(std::has_virtual_destructor<T>::value);
    CHECK((std::is_base_of<Action, T>::value));
    CHECK_DTOR_SLOT(T);
    CHECK_SLOT(T, TypeName,     0x08/8);
    CHECK_SLOT(T, Type,         0x10/8);
    CHECK_SLOT(T, Persist,      0x18/8);
    CHECK_SLOT(T, AbstractImpl, 0x20/8);
}

UNIT_TEST(act_leaf_animatenaturalresource) {
    CheckLeafShape<AnimateNaturalResource>();   // create 0x140021dc0, table 0x14015fb10
    CHECK_SLOT(AnimateNaturalResource, Execute,     0x28/8);   // [5] 0x1400c55b0
    CHECK_SLOT(AnimateNaturalResource, CheckEntity, 0x40/8);   // [8] 0x1400c55a0
    CHECK_SLOT(AnimateNaturalResource, GetTarget,   0x48/8);   // [9] 0x1400c5560
}

UNIT_TEST(act_leaf_completeroad) {
    CheckLeafShape<CompleteRoad>();   // create 0x140021ec0, table 0x14015fb70
}

UNIT_TEST(act_leaf_completetask) {
    CheckLeafShape<CompleteTask>();   // create 0x140021fc0, table 0x14015fbd0
    CHECK_SLOT(CompleteTask, CheckEntity, 0x40/8);   // [8] 0x1400c5ac0
    CHECK_SLOT(CompleteTask, GetTarget,   0x48/8);   // [9] 0x1400c5a50
}

UNIT_TEST(act_leaf_consumerawmaterial) {
    CheckLeafShape<ConsumeRawMaterial>();   // create 0x1400220c0, table 0x14015fc20
    CHECK_SLOT(ConsumeRawMaterial, CheckEntity, 0x40/8);   // [8] 0x1400c5e30
    CHECK_SLOT(ConsumeRawMaterial, GetTarget,   0x48/8);   // [9] 0x1400c5f10
}

UNIT_TEST(act_leaf_createnaturalresource) {
    CheckLeafShape<CreateNaturalResource>();   // create 0x1400221d0, table 0x14015fca0
    CHECK_SLOT(CreateNaturalResource, GetTarget, 0x48/8);   // [9] 0x1400c6610
}

UNIT_TEST(act_leaf_createrawmaterial) {
    CheckLeafShape<CreateRawMaterial>();   // create 0x1400222d0, table 0x14015fd10
    CHECK_SLOT(CreateRawMaterial, GetTarget, 0x48/8);   // [9] 0x1400c6aa0
}

UNIT_TEST(act_leaf_dropoffrawmaterial) {
    CheckLeafShape<DropOffRawMaterial>();   // create 0x140022600, table 0x14015fd80
    CHECK_SLOT(DropOffRawMaterial, CheckEntity, 0x40/8);   // [8] 0x1400c72f0
    CHECK_SLOT(DropOffRawMaterial, GetTarget,   0x48/8);   // [9] 0x1400c7270
}

UNIT_TEST(act_leaf_huntanimal) {
    CheckLeafShape<HuntAnimal>();   // create 0x140022700, table 0x14015fde0
    CHECK_SLOT(HuntAnimal, Execute,   0x28/8);   // [5] 0x1400c8240
    CHECK_SLOT(HuntAnimal, Enter,     0x30/8);   // [6] 0x1400c7de0
    CHECK_SLOT(HuntAnimal, GetTarget, 0x48/8);   // [9] 0x1400c7d60
}

UNIT_TEST(act_leaf_moveto) {
    CheckLeafShape<MoveTo>();   // create 0x1400228c0, table 0x14015fe40
    CHECK_SLOT(MoveTo, Execute,     0x28/8);   // [5] 0x1400c8570
    CHECK_SLOT(MoveTo, Enter,       0x30/8);   // [6] 0x1400c8970
    CHECK_SLOT(MoveTo, Exit,        0x38/8);   // [7] 0x1400c8520
    CHECK_SLOT(MoveTo, CheckEntity, 0x40/8);   // [8] 0x1400c84d0
    CHECK_SLOT(MoveTo, GetTarget,   0x48/8);   // [9] 0x1400c8480
    CHECK_EQ(offsetof(MoveTo, ticket) + offsetof(PathTicket, token), (size_t)0x24);
}

UNIT_TEST(act_leaf_performwork) {
    CheckLeafShape<PerformWork>();   // create 0x140022b90, table 0x14015fea0
    CHECK_SLOT(PerformWork, Execute,     0x28/8);   // [5] 0x1400c93c0
    CHECK_SLOT(PerformWork, Exit,        0x38/8);   // [7] 0x1400c9370
    CHECK_SLOT(PerformWork, CheckEntity, 0x40/8);   // [8] 0x1400c9320
    CHECK_SLOT(PerformWork, GetTarget,   0x48/8);   // [9] 0x1400c92d0
}

UNIT_TEST(act_leaf_pickuprawmaterial) {
    CheckLeafShape<PickupRawMaterial>();   // create 0x140022c60, table 0x14015ff10
    CHECK_SLOT(PickupRawMaterial, GetTarget, 0x48/8);   // [9] 0x1400c9860
}

UNIT_TEST(act_leaf_pickupstoredrawmaterial) {
    CheckLeafShape<PickupStoredRawMaterial>();   // create 0x140022d60, table 0x14015ff80
    CHECK_SLOT(PickupStoredRawMaterial, CheckEntity, 0x40/8);   // [8] 0x1400c9c70
    CHECK_SLOT(PickupStoredRawMaterial, GetTarget,   0x48/8);   // [9] 0x1400c9bf0
}

UNIT_TEST(act_leaf_playanimation) {
    CheckLeafShape<PlayAnimation>();   // create 0x1400011f0, table 0x140158950
    CHECK_SLOT(PlayAnimation, Execute,     0x28/8);   // [5] 0x140042400
    CHECK_SLOT(PlayAnimation, CheckEntity, 0x40/8);   // [8] 0x1400423f0
    CHECK_SLOT(PlayAnimation, GetTarget,   0x48/8);   // [9] 0x1400423b0
}

UNIT_TEST(act_leaf_removeanimal) {
    CheckLeafShape<RemoveAnimal>();   // create 0x140022e70, table 0x14015ffe0
    CHECK_SLOT(RemoveAnimal, CheckEntity, 0x40/8);   // [8] 0x1400ca760
    CHECK_SLOT(RemoveAnimal, GetTarget,   0x48/8);   // [9] 0x1400ca720
}

UNIT_TEST(act_leaf_removenaturalresource) {
    CheckLeafShape<RemoveNaturalResource>();   // create 0x140022f70, table 0x140160050
    CHECK_SLOT(RemoveNaturalResource, CheckEntity, 0x40/8);   // [8] 0x1400caaa0
    CHECK_SLOT(RemoveNaturalResource, GetTarget,   0x48/8);   // [9] 0x1400caa20
}

UNIT_TEST(act_leaf_sendcomponentmessage) {
    CheckLeafShape<SendComponentMessage>();   // create 0x140023070, table 0x1401600c0
    CHECK_SLOT(SendComponentMessage, GetTarget, 0x48/8);   // [9] 0x1400cae80
}

UNIT_TEST(act_leaf_showtool) {
    CheckLeafShape<ShowTool>();   // create 0x1400013b0, table 0x1401589b0
    CHECK_SLOT(ShowTool, Exit,        0x38/8);   // [7] 0x140042700
    CHECK_SLOT(ShowTool, CheckEntity, 0x40/8);   // [8] 0x1400426d0
    CHECK_SLOT(ShowTool, GetTarget,   0x48/8);   // [9] 0x140042680
}

// ENDGAME B.3.9 EXA: the citizen-AI builders construct PerformWork (the
// parameterised ctor 0x1400c9720) and PlayAnimation (default ctor + retail's
// inlined leaf stores) with `::new (MemAlloc(size, 8))` instead of the
// `??0Internal` door plus two stamps. The expressions are checked unevaluated.
template <typename T, typename... A>
using ExaNew = decltype(::new (std::declval<void*>()) T(std::declval<A>()...));
template <typename, typename T, typename... A> struct ExaCanNew : std::false_type {};
template <typename T, typename... A>
struct ExaCanNew<std::void_t<ExaNew<T, A...>>, T, A...> : std::true_type {};

static_assert(ExaCanNew<void, PerformWork, CitizenComponent* const*, float, uint32_t, int64_t, bool, int>::value,
              "PerformWork's parameterised ctor is placement-constructible");
static_assert(ExaCanNew<void, PlayAnimation>::value, "PlayAnimation is default placement-constructible");

}  // namespace
