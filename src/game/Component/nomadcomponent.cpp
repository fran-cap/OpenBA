// src/game/Component/nomadcomponent.cpp
// Clean reimplementation of NomadComponent lifecycle methods from Application-x64.exe.
//
// NomadComponent (0x78 bytes) — Component with SHIFTED controller at +0x58.
// Fields: +0x28=IBP, +0x30=Vector3, +0x3C=Vector3, +0x48=Time, +0x50=int
//
// Vtable at 0x14015a020. Two non-trivial overrides:
//   Shutdown (0x14004fc40) — Serialize IBP + 2 Vector3 + int + Time
//   OnCreate (0x14004fd20) — Register with nomad list
//

#include "err_line.h"   // HITCH_POSTDRAIN: one write per diagnostic line
#include "Dialog/EntityUI.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/PlayerToolManager.h"
#include "Dialog/exe_bodies.h"   // Toolbar_ShowGroup_Impl
#include "Player/exe_bodies.h"   // Avl38SetFindRemove (0x14002c690)
#include "math/Math.h"
#include "ui/UserInterface.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "Entity/ComponentEntity.h"   // ENDGAME B.3.9 CB: ComponentEntity::TypeId()
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "ui/DialogInstance.h"   // ENDGAME B.3.6 UI callers (K4)
#include "Player/Tool.h"          // Tool::TypeId()        (ENDGAME B.3.9 RF, cell 0x1401c6eb8)
#include "Player/ToolSetting.h"   // ToolSetting::TypeId() (ENDGAME B.3.9 RF, cell 0x1401c6ef0)
#include "object/TypeInfo.h"   // ENDGAME B.3.9 SS2: Object::Factory::_singleton
#include "Component/exe_bodies.h"

// Helpers, called by name (B3R): Object::Base::IsDerivedFrom (import slot 0x140152a98),
// NomadComp_FUN_140103480 (0x140103480, below), Toolbar_ShowGroup_Impl (0x140102b60),
// Avl38SetFindRemove (0x14002c690).

// ============================================================
// Reimpl of FUN_140103480 (Component reg wave "people")
// Ghidra mislabels this "TradeWork_impl_3480"; its real callers are entity-removal
// paths (CitizenComponent_OnRemove, DestroyDescription helpers, NomadComponent_OnCreate):
// it tears down any controller/trade reference for the entity. Steps:
//   1. If the controller at +0x28 reports active (vtable+0x50), call FUN_140102b60(self,0).
//   2. TradeWork_impl_c690 detaches the entity from the work list (+0x60).
//   3. Walk the UI's open dialogs; for any whose controller derives from
//      TypeId_PageToggleUI (DAT_1401c4f00) and whose bound entity (+0x30) == param_2,
//      close that dialog and return.
// Sub-deps + the member-return UI calls resolved via .rdata dispatch slots.
// Non-static for registration. Faithful structural port of 0x140103480.
// ============================================================
// vtable+0x50 takes TWO arguments — (self, entityRef) — NOT one.
//
// Retail's slot is EditTool_Slot10 (0x1400fd290), reached here because the object at
// self+0x28 is the EditTool holding the open info dialog:
//     bool EditTool_Slot10(longlong self, longlong *entityRef) {
//         CheckPtr(self + 0x30);
//         return *entityRef == *(longlong *)(self + 0x30);   // <-- DEREFS ARG 2
//     }
// i.e. "is the entity I have selected the one being removed?". TradeWork_impl_3480 sets
// `local_res8 = *(longlong *)param_2` immediately BEFORE the call and passes &local_res8;
// Ghidra renders the call as `(...)()` because it cannot recover args through the
// unrecovered function-pointer type, which is what made this look like a one-arg getter.
//
// Declaring it one-arg left RDX holding whatever the previous call put there, and the
// callee dereferenced it. On the FIRST invocation that is 0x140152a90 — the last argument
// to ResolveAddr inside Ensure3480() — so the crash presented as "a raw exe dispatch-slot
// VA dereferenced as a pointer" and sent the investigation after an unregistered slot.
// The fault address was stale-register garbage, not a resolve failure. (Same trap family
// as reference_byvalue_return_hidden_rdx.)
// The slot is Tool [10] IsSelected(uint64_t* entityRef) (Player/Tool.h), called by name below.
// 0x140102b60 is Toolbar_ShowGroup_Impl, 0x14002c690 Avl38SetFindRemove.
// slot 0x140151768 -- member, non-trivial return AND a THIRD argument Ghidra hides.
// Retail 0x140103480 at 0x140103517: `MOV R8D,[0x1401c4f00]` (EntityUI) immediately
// before `CALL [0x140151768]`. The mangled name confirms it: ...@Object@@I@Z -- the
// trailing `I` is that uint32 type-id FILTER. Declared with two parameters, the call
// passes whatever the previous call left in R8 as the filter, and GetController
// returns nothing for a garbage id -- silently, with no crash and no log.
// See reference_getcontroller_third_arg_is_a_type_filter.

void PlayerToolManager::OnEntityRemoved(ComponentEntity* const* entity) {
    if (currentTool != nullptr) {
        // Retail's local_res8: seeded from *entity BEFORE the call, and passed by
        // address as the slot's second argument (see the typedef note above).
        ComponentEntity* entityRef = *entity;
        char active = currentTool->IsSelected(&entityRef);   // slot 10
        // 0x140102b60 is EntityUI::SetEditMode / Toolbar::ShowGroup — resetting the
        // active tool to mode 0 when the entity it was bound to goes away. This call
        // was DEAD for the whole life of the project (0x140102b60 was ported but never
        // registered); registering it for GameUpdateLoop::Shutdown woke it up. Waking
        // it coincided with the workplace/residence panels going inert, so it is gated
        // while that is confirmed: SetEditMode(pc, 0) tears down the live tool
        // controller at +0x28, which is what those panels' spinners hang off.
        if (active != '\0') {
            static int s_hits = 0;
            if (++s_hits <= 16) {
                ErrLine("[NomadB60] entity-removed -> SetEditMode(pc,0) hit#%d self=%p\n",
                        s_hits, static_cast<void*>(this));
                fflush(stderr);
            }
            SelectTool(nullptr);   // 0x140102b60
        }
    }

    ComponentEntity* key = *entity;
    Avl38SetFindRemove(&field_0x60, &key);   // 0x14002c690

    UI::UserInterface* ui = GameUpdateLoop::_singleton->m_userInterface;
    // HISTORY, kept because it cost two bugs. Retail reaches this list through
    // GetFirstDialog, which returns a struct BY VALUE, so the ABI passes a hidden
    // return-buffer pointer in RDX and the callee WRITES through it. Runtime-x64.dll
    // 0x180031150:
    //   ?GetFirstDialog@UserInterface@UI@@QEAA?AUIterator@?$LinkedList@...@@XZ
    //   void *__thiscall GetFirstDialog(UserInterface *this, void *retval)
    //   { *(undefined8 *)retval = *(undefined8 *)this; return retval; }
    // An early one-arg typedef here left RDX stale and that store wrote 8 bytes through
    // whatever the previous call had left there — a memory-corrupting WRITE, worse than
    // the READ fault fixed above. It also took the RAX return (which is `retval`, i.e.
    // the buffer's ADDRESS) as the iterator, one dereference too few.
    // LIVE as of 2026-09-04 via a by-name fallback to fn_UI_GetFirstDialog; since
    // B.3.2 the walk is the member iteration below instead, which is the same read
    // (`_dialogs._head`) with no slot, no typedef and no hidden return buffer to get
    // wrong. The GetController call below was fixed to pass its type-id filter in the
    // 2026-09-04 change — that had to land FIRST, because reviving the walk with a
    // two-argument typedef would have called GetController with a stale R8 as the
    // filter.
    // Bounded proof-of-life: this walk was dead for the whole life of the project, so
    // "it runs at all" is the thing to confirm first. Reports the dialog count it sees
    // and whether it closed one.
    // Verified live 2026-09-04 on a Remove of a starting building:
    //   [NomadDlgWalk] #1 visited=3 CLOSED the removed entity's panel
    // with 0x140151ca0 and 0x140151550 both still logging NOT IMPLEMENTED, i.e. the
    // by-name fallbacks are what carry it. Kept bounded so a future regression is one
    // grep away, but it is not free: leave the cap small.
    static int s_walks = 0;
    const bool traceWalk = (++s_walks <= 8);
    int visited = 0, closed = 0;
    for (auto it = ui->_dialogs.GetFirst(); it; ++it) {
        ++visited;
        if (it.node->element != nullptr) {
            // R8 = EntityUI (retail `MOV R8D,[0x1401c4f00]` at 0x140103517). The
            // IsDerivedFrom(EntityUI) narrow just below is retail's own second check,
            // NOT a substitute for this filter -- without it the call returns nothing
            // and the narrow never gets anything to reject.
            Object::InternalPtr<UI::DialogController> ctrl(
                it.node->element->GetController(
                    EntityUI::TypeId()));   // DAT_1401c4f00
            EntityUI* found = nullptr;
            if (ctrl.Get() != nullptr) {
                if (::Object::IsDerivedFromNullSafe(ctrl, EntityUI::TypeId()))
                    found = static_cast<EntityUI*>(ctrl.Get());
            }
            if (found != nullptr) {
                ComponentEntity* bound = found->entity;
                if (bound == *entity) {
                    Object::InternalPtr<UI::DialogInstance> dlgPtr(it.node->element);
                    ui->CloseDialog(&dlgPtr);
                    closed = 1;
                    if (traceWalk) {
                        ErrLine("[NomadDlgWalk] #%d visited=%d CLOSED the removed "
                                        "entity's panel (dlg=%p)\n", s_walks, visited, static_cast<void*>(dlgPtr.Get()));
                        fflush(stderr);
                    }
                    return;
                }
            }
        }
    }
    if (traceWalk) {
        ErrLine("[NomadDlgWalk] #%d visited=%d closed=%d (no open panel bound to "
                        "the removed entity)\n", s_walks, visited, closed);
        fflush(stderr);
    }
}


// ============================================================================
// ENDGAME B.3.8 MI-A — NomadComponent as a REAL class (NomadComponent.h).
//
// Before: CONTROLLER_LIVE_FACTORY_STATES + CONTROLLER_INSTALL_SERIALIZE (a 48-slot
// table re-filled at every create), `Inert_ScalarDtor` in slot 0 (only `~Internal`), the
// ControllerLive::Dispatcher<NomadCtrlTag> table stamped into raw bytes at +0x58.
// After: the compiler's 18-slot vtable, a real constructor and destructor
// chain, the controller a member object (B38_RULES D1, D2, D6).
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/NomadComponent.h"

// The nomad walk-in/loiter/walk-out state machine (nomadcomponent_states.cpp).
// Node at +0x58 (slot 17 0x140009060), default state 0 (slot 16 0x140003730). The
// state table is built ONCE at static-init time, as retail's static initializer
// does (B38 D6/D7).
extern "C" void NomadCtrl_PopulateTable();
static const bool s_nomadStateTableBuilt = (NomadCtrl_PopulateTable(), true);

// 0x14004fdb0. Component() is the CALL 0x1401188b0; the stamps are the compiler's;
// +0x28..+0x50 = 0; `_controller(this)` is the member's back-pointer at +0x70;
// then `_flags |= 2` (0x82).
NomadComponent::NomadComponent()
    : hall(nullptr), spawnPoint(0.0f, 0.0f, 0.0f), meetPoint(0.0f, 0.0f, 0.0f), wanderTimer{0},
      gender(0), _pad54{}, _controller(this) {
    OrFlagsInCtor(0x2);
}

// 0x14004fd50: restamp + ~Internal, no own work.
NomadComponent::~NomadComponent() = default;

// create 0x140009020: MemAlloc(0x78, 8), then the ctor.
::Object::Base* NomadComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(NomadComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) NomadComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* NomadComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) NomadComponent());
}

// ============================================================
// NomadComponent::Persist (vtable slot [3], 0x14004fc40)
// ============================================================
// Component_Serialize + empty controller serializer + second serializer:
//   IBP at +0x28, Vector3 at +0x30, Vector3 at +0x3C, int at +0x50, Time at +0x48

void NomadComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    IO::Stream* st = &stream;

    // Empty controller serializer block
    { IO::Serializer scope(*st, 0); }

    // Main data serializer
    IO::Serializer ser(*st, 0);

    // Serialize the hall IBP at +0x28.
    hall.InternalBasePtr::Serialize(ser);

    // Validate IBP after deserialize
    // ENDGAME B.3.9 CB: retail validates this IBP with IsDerivedFrom(ptr, [0x1401c5d70]) =
    // ComponentEntity::TypeId() (Persist 0x14004fc40, at 0x14004fca5). The tree read 0x1401c72e8 instead -- the NAME
    // field (+8) of Description's record, whose only retail reader is Description's
    // TypeName slot 0x140031c60 -- which is unregistered, so the check never ran.
    // NOT RETAIL (F-143): null and type tests on the held Internal*, before any
    // downcast to ComponentEntity* (`hall->` was one); the answer is the same.
    if (!hall.IsNull()) {
        // slot 0x140152a98 = ?IsDerivedFrom@Base@Object@@QEBA_NI@Z
        if (!::Object::IsDerivedFromNullSafe(hall, ComponentEntity::TypeId())) {
            hall = nullptr;
        }
    }

    // Serialize Vector3 at +0x30 (12 bytes)
    spawnPoint.Serialize(ser);

    // Serialize Vector3 at +0x3C (12 bytes)
    meetPoint.Serialize(ser);

    // Serialize int at +0x50
    ser.Serialize(gender, nullptr);

    // Serialize Time at +0x48
    wanderTimer.Serialize(ser);
}

// [7] 0x14004fd20 — the removed-from-world broadcast: hand the owner entity to the
// player/selection manager's removal notify (0x140103480, "TradeWork_impl_3480" in
// Ghidra: drop the selection if it is this entity, close its EntityUI dialogs).
// Retail, whole body:
//     MOV RAX,[RCX+0x10]; MOV RCX,[0x1401c3dd8]; LEA RDX,[RSP+0x30]
//     MOV RCX,[RCX+0x120]; MOV [RSP+0x30],RAX; CALL 0x140103480
// Unguarded, as retail. The notify is NomadComp_FUN_140103480, called by name.
void NomadComponent::OnRemove() {
    ComponentEntity* entityIBP = _entity.Get();
    GameUpdateLoop::_singleton->m_toolManager->OnEntityRemoved(&entityIBP);   // 0x140103480
}

DEFINE_EXE_TYPE(NomadComponent, "ControllerComponent")
