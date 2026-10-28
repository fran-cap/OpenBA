// src/game/Component/statusiconcomponent.cpp
// Clean reimplementation of StatusIconComponent lifecycle methods from Application-x64.exe.
//
// StatusIconComponent (0x108 bytes) displays floating status icons above entities
// (e.g., hunger, cold, disease). Embeds a Graphics::Geometry sub-object at +0x28
// (0xC0 bytes, with overridden vtable at 0x14015f0c0).
//
// Vtable at 0x140164620. Non-trivial overrides:
//   Persist  (0x14012bff0) — Serializes the billboard's activeMask (+0xF0 as uint)
//   Startup  (0x14012bed0) — Reads sprite data from building description
//   Tick     (0x14012bbd0) — Manages Scene add/remove, positions icon (very complex)
//   Slot11   (0x14012bf40) — post-load: sprite setup, adds to Scene (ShowAfterLoad)
// and the non-virtual SetFlag (0x14012c3f0).
//
// Decompiled from Application-x64.exe using Ghidra.

// graphics/*.h FIRST: they pull src/engine/volume/Volume.h, whose global-scope
// `struct Rectangle` is hidden by <wingdi.h>'s `Rectangle()` once <windows.h> has
// been seen through resolve.h.  ENDGAME B.3.5 caller batch I2.
#include "graphics/Scene.h"
#include "graphics/Geometry.h"
#include "Component/StatusIconComponent.h"
#include <cstdint>
#include "Component/StatusIconDescription.h"
#include "Component/ModelComponent.h"
#include "Component/MultiObjectComponent.h"
#include "Component/MapComponent.h"
#include "Entity/ComponentEntity.h"
#include "scene_watch.h"
#include "math/Math.h"
#include "ext/registry.h"   // Ext::Fix().icon_slide
#include "system/Value.h"
#include "ui/UserInterface.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "Game/exe_bodies.h"   // B3R STORAGE: g_gameSettings
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"              // Map::size
#include "ext/render_culling/render_culling.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdio>
#include <new>
#include "ui/SpriteSheet.h"   // ENDGAME B.3.6 UI callers (K4)
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"   // ActHunt_NotifyEntityChanged_c3470

// Exe helpers, by retail address: 0x1400aa440 StatusIconGeometry::Setup (below),
// 0x1400c3470 ActHunt_NotifyEntityChanged_c3470 (entity notification), 0x140152b00
// ??0Time@System@@QEAA@M@Z (RtSystem_Time_Ctor), 0x140004680 FindComponent<MapComponent>,
// 0x14009f3e0 Reimpl_MapGetTerrainPos (grid cell -> world position).

// B6: SI_Initialised (a test of the billboard geometry's vptr, "[STANDALONE GUARD D4]") is gone.
// The real ctor (StatusIconComponent::StatusIconComponent, retail 0x14012c0c0) builds the
// geometry at construction, so the test was always true; retail has no such test in
// SetFlag 0x14012c3f0, OnRemove or Tick.

// The graphics scene the icons register with: GameUpdateLoop +0x60.
static Graphics::Scene* StatusIcon_Scene() {
    return GameUpdateLoop::_singleton->m_graphicsScene;
}

// ============================================================
// StatusIconGeometry::Setup (0x1400aa440): the geometry-bounds setup
// ============================================================
// Stores the sprite parameters into the billboard and computes its axis-aligned
// bounds. half = size * 0.5; bounds box is min(-half,-half,0) .. max(half,half,
// count*size). 1:1 port; the only callee is Graphics::Geometry::SetBounds.
void StatusIconGeometry::Setup(float size, int count, uint8_t constant, UI::SpriteSheet* sheet) {
    float half = size * 0.5f;

    iconSize = size;
    _materialTypeFlags = 2;
    spriteCount = count;
    spriteSheet = sheet;
    activeMask = 0;
    constantSize = constant;   // the original stores the low byte of its fourth argument

    ::Box box;
    box.min.x = -half;
    box.min.y = -half;
    box.min.z = 0.0f;
    box.max.x = half;
    box.max.y = half;
    box.max.z = (float)count * size;

    SetBounds(box);
}

// ============================================================
// Helper: the StatusIconDescription (the component's +0x18, IsDerivedFrom checked)
// ============================================================
static StatusIconDescription* GetStatusIconDescription(const StatusIconComponent* self) {
    Description* obj = self->_description;
    if (!obj) return nullptr;

    bool derived = Object::IsDerivedFromNullSafe(obj, StatusIconDescription::TypeId());
    return derived ? static_cast<StatusIconDescription*>(obj) : nullptr;
}


// ============================================================
// StatusIcon_EnsureSprite — retail's lazy sprite setup (component vtable slot 4)
// ============================================================
// Slot 4 (0x14012bed0, StatusIconComponent::Startup) is the ONLY thing that
// ever writes the embedded Geometry's sprite/material from the StatusIconDescription
// — StatusIconGeometry::Setup has exactly one caller in the whole exe.
//
// Retail dispatches slot 4 from TWO places:
//   * ComponentEntity creation (FUN_1400c30a0, called only from CreateEntity), and
//   * slot 11 (0x14012bf40), on EVERY call, immediately before it adds the geometry
//     to the Scene.
// The second one is what makes icons work after a LOAD: a stream-restored component
// never goes through CreateEntity, so it never runs slot 4 at construction (see
// [[reference_ctor_scalars_in_startup_are_unset_after_load]]). Without the slot-11
// leg its geometry is added to the Scene with NO sprite and draws nothing — which is
// exactly what a loaded world (the tutorial, any .sav) showed: zero icons, no error.
//
// Retail brackets the call with a save/restore of +0xF0 because slot 4 clobbers it.
//
// ⚠ CALL THE BODY NON-VIRTUALLY, NEVER DISPATCH VTABLE SLOT 4 HERE.
// Retail's slot 4 is exactly the sprite setup and nothing else. Until ENDGAME B.3.8
// VIS OUR slot 4 ALSO re-ran the construction half (the Geometry ctor door on +0x28
// and the scene-object id at +0x100 = -1), so dispatching it mid-life reset a live
// component. The real class's slot 4 no longer does, but the rule stands: the call
// is qualified.
//
// It used to be called from StatusIcon_Toggle too (a second 0x14012c3f0 that
// re-ran slot 4 before its "not in the scene yet?" test; Toggle is gone, the citizen calls
// SetFlag). With +0x100 reset to -1 that test passed EVERY time,
// so Toggle re-Scene::AddObject'd the same geometry on every call and never removed
// the previous node. Measured 2026-09-04 on the tutorial: 16 icon geometries added
// 119,143 times, the Graphics::Scene growing a steady ~75 nodes/frame to 121,856
// nodes in a 5,355-entity world, StatusIconGeom_Draw called ~96,000 times per frame
// (1.9M draw calls/sec, climbing) and taking 67% of process time — the frame rate
// decayed from 60 fps to 8 over about two minutes.
//   [SceneAdd] StatusIconComponent::Show  calls=119143  distinct-geoms=16
//   [IconTrace] slot4 CLOBBERED sceneId comp=...  4849 -> -1
static void StatusIcon_EnsureSprite(StatusIconComponent* c) {
    uint32_t saved = c->_geometry.activeMask;
    c->StatusIconComponent::Startup();
    c->_geometry.activeMask = saved;
}

// ============================================================
// StatusIconComponent::ShowAfterLoad — the body of vtable slot [11] (0x14012bf40),
// the post-load fixup (EntityList 0x1400c4730)
// ============================================================
// A loaded component never ran slot 4 (Startup), so its billboard has no sprite:
// re-run slot 4 (`CALL [RAX+0x20]`) with the +0xF0 mask saved around it (Setup
// clears it). Then, when the icon is not in the scene (+0x100 < 0) and the
// restored mask has bits: AddObject the billboard into the graphics scene
// (GameUpdateLoop +0x60, import 0x140151370), +0x100 = its id, flag bit 2 (0x04)
// with the entity notified on a low-word change (0x1400c3470), and slot 8 (Tick,
// `CALL [RAX+0x40]`) with Time(0) positions it. Retail reads the singleton and
// the scene unguarded, and does NOT test the status-icon setting here (SetFlag
// does).
//
// FIX icon_slide (docs/retail_bugs.md section 1): retail 0x14012bf40 calls Scene::AddObject
// FIRST and positions the icon afterwards (slot 8, Tick 0x14012bbd0 -> Geometry::SetTransform
// 0x1800155a0), so for one sim tick the geometry's previous transform is the origin and every
// frame drawn in that tick lerps from the map centre to the building. The corrected arm
// positions the geometry BEFORE AddObject ("position before AddObject in the component, which
// is an exe-side deviation and must be documented as such"): AddObject's Clean then turns
// dirty bit 0 into the rolled bit 3, and the tick's Scene::Clean copies current into previous
// before anything draws it. Geometry::Clean / SetTransform are untouched; the retail Tick
// below still runs, and its SetTransform of the same transform changes nothing.
static void StatusIcon_PlaceIcon(StatusIconComponent* c);
static void StatusIcon_SetIconTransform(StatusIconComponent* c, ComponentEntity* entity, Component* model);
void StatusIconComponent::ShowAfterLoad() {
    const bool fixIconSlide = Ext::Fix().icon_slide;
    StatusIcon_EnsureSprite(this);   // slot 4 with +0xF0 saved and restored
    const uint32_t savedMask = _geometry.activeMask;
    if (iconId >= 0 || savedMask == 0) return;

    if (fixIconSlide) StatusIcon_PlaceIcon(this);   // FIX icon_slide: position, then add
    uint32_t newId = StatusIcon_Scene()->AddObject(&_geometry);
    SceneWatch_Note(newId, &_geometry, "StatusIconComponent::Slot11");
    iconId = static_cast<int32_t>(newId);

    uint32_t oldFlags = _flags;
    uint32_t newFlags = oldFlags | 0x04u;
    _flags = newFlags;
    if (((newFlags ^ oldFlags) & 0xFFFF) != 0 && _entity != nullptr) {
        _entity.Get()->RefreshEntityFlags();   // 0x1400c3470
    }

    System::Time timeObj;
    RtSystem_Time_Ctor(&timeObj, 0.0f);   // import 0x140152b00
    Tick(&timeObj);   // vtable +0x40 (slot 8), a virtual dispatch in retail
}

// ============================================================
// StatusIconComponent::SetFlag (0x14012c3f0; the tree's CitizenComp_FUN_14012c3f0,
// moved from citizencomponent.cpp)
// ============================================================
// Set/clear bit `flagIndex` in the status mask (+0xF0); when the mask becomes
// empty/non-empty, add/remove the geometry from the scene and toggle the +0x100
// scene id and the +0x20 flag bit 4, then notify the owning entity.
//
// FIX icon_slide (docs/retail_bugs.md section 1): retail 0x14012c3f0 has slot 11's shape --
// AddObject, then Tick (slot 8) positions -- and it is the path a running game's icons ("work
// stopped", "no workers") take, so the corrected arm positions before AddObject here too.
void StatusIconComponent::SetFlag(uint8_t flagIndex, char value) {
    const bool fixIconSlide = Ext::Fix().icon_slide;
    if ((((_entity->_entityFlags >> 10) & 1) == 0) || (value == '\0')) {
        uint32_t bit = 1u << (flagIndex & 0x1f);
        if (value == '\0') {
            _geometry.activeMask &= ~bit;
        } else {
            _geometry.activeMask |= bit;
        }
        uint32_t changed;
        if (static_cast<int32_t>(_geometry.activeMask) == 0) {
            // [STANDALONE GUARD] A StatusIconComponent that never ran StartupInit still
            // carries the inert factory's zero-fill: the embedded geometry's vtable
            // (+0x28) is 0 and the scene id (+0x100) is 0. That 0 means "never
            // registered", NOT scene object 0 — and Scene::RemoveObject(scene, 0)
            // removes whichever object genuinely owns id 0, after which the engine's list
            // walk spins: an in-world FREEZE, not a crash (the failure mode fixed at source
            // for HighlightComponent in 0f97d16). Repair the sentinel instead of removing.
            // Reachable on the default path since the pause port graduated: SetAccepting's
            // accepting=false branch CLEARS icon flags 2/3/4, and clearing the last flag on
            // an uninitialised icon lands exactly here.
            //
            // WARNING: the old note here claimed "the AddObject side below needs no guard -
            // an uninitialised +0x100 == 0 fails its `< 0` test". That was FALSE, and it was
            // falsified BY THE LINE BELOW: repairing the sentinel to -1 is exactly what makes
            // `+0x100 < 0` true, so the next time a flag is SET this function registered a
            // geometry whose vtable is still 0 into the Graphics::Scene.
            // `[SceneWatch] BAD=1 ... vtable=0000000000000000` on a 21-minute load soak
            // (2026-08-29) is that node, and it is the `EXEC of <heap>` in Scene::Clean.
            // The upstream fix is the real ctor (StatusIconComponent::StatusIconComponent),
            // which builds the geometry at CONSTRUCTION where retail's ctor 0x14012c0c0
            // has it, so the geometry is always built; the gate that tested it is gone (B6).
            //
            // CitizenComponent::Update's five flag sites (0x140113254 .. 0x14011334b) call
            // this body directly, as retail does.
            if (-1 < iconId) {
                StatusIcon_Scene()->RemoveObject(static_cast<uint32_t>(iconId));
                iconId = -1;
            }
            uint32_t before = _flags;
            uint32_t after = before & 0xfffffffb;
            _flags = after;
            changed = after ^ before;
        } else {
            // The status-icon setting [0x1401c3de0]+0xc, read unguarded
            // (0x14012c45d..0x14012c468), as retail reads it (row 1263).
            if (g_gameSettings->statusIconEnable != '\0' && iconId < 0) {
                if (fixIconSlide) StatusIcon_PlaceIcon(this);   // FIX icon_slide: position, then add
                uint32_t id = StatusIcon_Scene()->AddObject(&_geometry);
                iconId = static_cast<int32_t>(id);
                SceneWatch_Note(id, &_geometry, "StatusIcon(12c3f0)::Add");
            }
            System::Time timeVal{0};
            RtSystem_Time_Ctor(&timeVal, 0.0f);
            Tick(&timeVal);   // vtable +0x40 (slot 8), a virtual dispatch in retail
            uint32_t before = _flags;
            uint32_t after = before | 4;
            _flags = after;
            changed = after ^ before;
        }
        if ((changed & 0xffff) != 0 && _entity != nullptr) {
            _entity.Get()->RefreshEntityFlags();   // 0x1400c3470
        }
    }
}

// ============================================================
// StatusIconComponent::ClearAllFlags (0x14012c2a0)
// ============================================================
// Retail walks the 32 bits of the mask from bit 0 and, for each one that is set, clears it with the body of SetFlag(bit, 0) inlined
// (remove the geometry from the scene when the mask reaches 0, notify the entity when the flag word changed). Its one caller is
// DestroyComponent state 3 enter, which strips every icon of a building that is about to be demolished, then sets flag 5.
void StatusIconComponent::ClearAllFlags() {
    for (uint32_t bit = 0; bit < 32; ++bit) {
        if ((_geometry.activeMask & (1u << bit)) != 0) SetFlag(static_cast<uint8_t>(bit), 0);
    }
}

// B4 FORWARD: outside callers in 16 files (Component: attendancecomponent*, buildcomponent_state8,
// citizenai_gather, citizen_needsupdate, destroycomponent_states, workcomponent,
// workplacecomponent; Work: consumeproduce, fishingwork, foresterwork, harvestarea,
// harvestorchard, herbalistwork, huntwork, pasturework)

// ============================================================
// Standalone bring-up (Phase A): faithful construction + diagnostics
// ============================================================
#include "Object/factory_registration.h"
#include <cstdlib>

// The embedded billboard (+0x28) is a real Graphics::Geometry subclass since
// ENDGAME B.3.8 VIS (StatusIconGeometry, StatusIconComponent.h; bodies in
// statusicon_geometry.cpp). The all-no-op fallback table and the minted-table
// copy it replaced are gone, and with them the Geometry ctor door this file
// resolved.

// Graphics::Geometry::SetTransform (Runtime export) — used by Tick positioning.

// Entity component finders: 0x140003f60 ModelComponent (type 0x1401c7a50) and
// 0x140004740 MultiObjectComponent (type 0x1401c7ac0) are replaced by the by-name
// lookup below; 0x140004680 -> MapComponent (type 0x1401c79e0).
// CharacterComponent_impl_f3e0 (0x14009f3e0, Reimpl_MapGetTerrainPos) — grid cell ->
// world position.

// ============================================================
// [STANDALONE GUARD] component-array entry sanity (2 deviations, kept deliberately)
// ============================================================
// Retail's component walks (0x140003f60, 0x140004740, 0x140004680 ...) dereference every
// entry of the owner entity's component array with NO validation, because in retail the
// array is always consistent: an entity's removal clears the weak refs that point at it
// before anything else can walk them. Our removal lifecycle does not guarantee that yet
// (dangling weak refs after destroy/upgrade teardown are a known open issue), so a
// half-torn-down building can hand these walks freed memory. Live crash 2026-07-25:
// pausing a workplace whose upgrade was in flight faulted on READ of 0xa — a small
// non-pointer value read out of the array and dereferenced as a component pointer.
//
// SCOPE (thinned 2026-07-25 after the buildui wrong-finder fix landed and a live pause
// ran clean with none of these firing). One deviation from the decompile remains (D4,
// the never-ran-StartupInit sentinel test, was removed in B6):
//   D2  the array ENTRY walked by StatusIcon_FindCompByName is range/alignment checked
//       before it is dereferenced — 0xa fails both (below 0x10000 and not 8-aligned),
//       as does every other small free-list-ish integer a reused block leaves behind.
//       This is the read that actually faulted, which is why it is the one kept.
//
// D2 TESTS ONLY THE ENTRY, AND ONLY AS A DATA POINTER — do not extend it. It briefly also
// tested the component's vtable pointer and the GetTypeId function pointer at vtable+0x10;
// that second one was a live-fire bug (2026-07-25, defaults-only build, healthy world):
// a CODE address has no alignment guarantee, and measured on this build only 30 of 132
// component GetTypeId functions are 8-aligned — GCC packs these tiny statics with no
// padding. The check therefore rejected ~77% of all component types and silently skipped
// LIVE components during the ModelComponent / MultiObjectComponent lookups, which can
// select the wrong positioning branch. Every component pointer, by contrast, comes from
// Memory::Allocate(size, 8) and every vtable is an 8-aligned data symbol (66/66 measured),
// so the entry test cannot misfire the same way.
// Everything else that was here has been removed and those paths are byte-faithful to
// retail again: the owner-entity gate that suppressed the scene ops, the self/width/notify
// plausibility checks, and a whole-array pre-check in front of the exe map finder. That
// last one was dropped on principle rather than because it was harmless: the SAME array is
// walked unguarded by every WorkBase_FindComponent_* call site in the tree, so guarding one
// of them defends nothing systemically — if that corruption returns it will fault
// elsewhere first, and the fix belongs in the removal lifecycle, not here.
static inline bool SI_Plausible(const void* p) {   // LEFT: void* is a raw address (diagnostic probe), not an object: in [0x10000, 0x7fffffffffff) and 8-aligned
    uintptr_t v = reinterpret_cast<uintptr_t>(p);  // CAST-OK: ptr_int
    return v >= 0x10000 && v < 0x00007fffffffffffull && (v & 7) == 0;
}

// Bounded "this should never happen" logging: the first few of each category go to
// standalone_error.log with the operands, then the category goes quiet so a per-tick
// caller cannot flood the log.
static bool SI_LogOnce(int* budget) {
    if (*budget <= 0) return false;
    --*budget;
    return true;
}

// PRECONDITION of D2, not a deviation in its own right: the per-entry check below reads
// THROUGH the array base, so `arr`/`count` have to be sane for it to mean anything. Retail
// reads both fields unguarded and so do we — this only rejects the values, it does not add
// a check the original lacks anywhere the original would have proceeded safely.
static bool SI_ComponentArray(const ComponentEntity* entity, int* outCount,
                              Object::InternalPtr<Component>** outArr) {
    if (!SI_Plausible(entity)) return false;
    int count = entity->_components._size;
    Object::InternalPtr<Component>* arr = entity->_components._data;
    if (count < 0 || count > 4096) return false;
    if (count > 0 && !SI_Plausible(arr)) return false;
    if (outCount) *outCount = count;
    if (outArr) *outArr = arr;
    return true;
}


// Removed-from-world handler (component vtable +0x38, idx 7). Entity teardown
// dispatches this via MarkForRemoval (0x1400c2ea0: every component with
// capability bit 1 — ours has 0xC2 — gets vtable+0x38 before the 0x400 bit is
// set). Remove the icon's scene object so the renderer's draw list never holds
// freed geometry — same crash class as the StorageDisplay pile meshes fixed
// 2026-07-20 (demolition → Draw-phase Transform::Lerp on freed memory). Retail
// additionally strips icons at demolition start (state-3 enter → ClearAllFlags, 0x14012c2a0) and shows flag 5;
// this teardown-time removal is the safety net that must always run.
void StatusIconComponent::OnRemove() {
    if (GameUpdateLoop::_singleton == nullptr) return;
    Graphics::Scene* scene = StatusIcon_Scene();
    int32_t sceneId = iconId;
    if (sceneId >= 0 && scene) {
        scene->RemoveObject((uint32_t)sceneId);
        iconId = -1;
    }
}

// The CTOR half of the spawn-time init (StatusIconComponent_CtorScalars until
// ENDGAME B.3.8 VIS) is the real ctor now — see StatusIconComponent::
// StatusIconComponent() at the end of this file. It MUST run at construction:
// retail's load path (ComponentEntity::Serialize 0x1400c3140) rebuilds a
// stream-restored component through the factory ctor and then calls ONLY
// RefreshEntityFlags — vtable +0x20 (Startup) is NEVER dispatched for it. See
// [[reference_ctor_scalars_in_startup_are_unset_after_load]].



// Resolve a component on the owner entity BY NAME (Object::GetTypeIdByName), mirroring
// CharacterComponent's CharComp_FindSiblingByName. This is the standalone-safe equivalent
// of the exe finders 0x140003f60 (ModelComponent) / 0x140004740 (MultiObjectComponent):
// those compare the .rdata type-id globals DWORD_1401c7a50 / c7ac0, which are 0xFFFFFFFF
// (UNFILLED) in standalone, so they return null for EVERY entity. Resolving the type id by
// name at runtime matches the established brick pattern and has no shared-global dependency.
//
// The scan itself is 1:1 with the exe finders (8-byte stride over entity+0x30, compare
// vtable+0x10 GetTypeId, IsDerivedFrom on the first match). The one deviation is
// [STANDALONE GUARD D2]: entries that cannot be a live component are SKIPPED instead of
// dereferenced, so a half-torn-down entity yields "component not found" rather than a
// fault. Retail would have had a fully-consistent array here.
// [StatusIcon] GUARD trace budgets (were function-local statics; B6-5).
static int s_siBadEntryBudget = 8;

// B.3.8 CB1b: the caller passes the class's TypeId() (a plain read), which replaces
// the per-name first-use cache this helper used to keep (B6-5); `typeName` is only
// for the guard trace.
static Component* StatusIcon_FindCompByName(const ComponentEntity* entity, uint32_t want, const char* typeName) {
    if (!entity) return nullptr;
    if (want == 0 || want == 0xFFFFFFFFu) return nullptr;

    int count = 0;
    Object::InternalPtr<Component>* arr = nullptr;
    if (!SI_ComponentArray(entity, &count, &arr)) return nullptr;

    for (int i = 0; i < count; i++) {
        Component* comp = arr[i].Get();
        if (!comp) continue;
        // [STANDALONE GUARD D2] the entry must look like a heap pointer before it is
        // dereferenced. This is the read that faulted on 0xa. A line here now means a
        // genuinely bad array entry — see the header note: it must NOT be extended to
        // test the vtable's function pointers.
        if (!SI_Plausible(comp)) {
            if (SI_LogOnce(&s_siBadEntryBudget)) {
                fprintf(stderr, "[StatusIcon] GUARD: entity=%p component[%d/%d]=%p is not a "
                        "plausible pointer (arr=%p) — skipping it during the %s lookup\n",
                        static_cast<const void*>(entity), i, count, static_cast<void*>(comp),
                        static_cast<void*>(arr), typeName);
                fflush(stderr);
            }
            continue;
        }
        uint32_t tid = comp->Type();
        if (tid == want)
            return Object::IsDerivedFromNullSafe(comp, want) ? comp : nullptr;
    }
    return nullptr;
}

// NOTE (2026-07-08): the per-frame occupancy poll `StatusIcon_DrainForceShow` and its
// deferred drive-list were removed. The building "no workers" warning (bit 4 / 0x10) is
// now raised by the genuine faithful event — WorkPlaceComponent_SetAccepting
// (workplacecomponent.cpp, port of Entity_impl_fc00) at the lodge-activation point — and
// cleared by WorkplaceComponent_Attach when a worker is seated. Citizen health icons are
// driven by CitizenComponent::Update. The status-icon user setting (settings byte +0xc)
// is already initialised to 1 at settings creation (game_main.cpp: settings[3]=1), so no
// per-frame poke is needed.

// ============================================================================
// ENDGAME B.3.8 VIS — StatusIconComponent as a REAL class (StatusIconComponent.h).
//
// Before: a 48-slot StatusIconComponent_LiveVtable filled per create (slots 7/8
// patched in by CtorScalars), Inert_ScalarDtor in slot 0, the billboard built by
// the Geometry ctor DOOR and re-stamped with a copy of the minted Geometry table.
// After: the compiler's 16-slot vtable, a real ctor/dtor chain (B38_RULES D1,
// D2) and the billboard as a real Graphics::Geometry member (D6).
// ============================================================================

// 0x14012c0c0: Component() is the CALL 0x1401188b0; the 0x140164620 stamp is the
// compiler's; the member's Geometry() is the `??0Geometry@Graphics@@` call and
// its 0x14015f0c0 stamp is the compiler's (StatusIconGeometry); +0x100 = -1
// (sceneObjId "not registered": a 0 there would make Scene::RemoveObject spin);
// flags |= 0xc2.
StatusIconComponent::StatusIconComponent() : _geometry(), iconId(-1), _pad104{} {
    OrFlagsInCtor(0xc2);
}

// ============================================================
// StatusIconComponent::Persist (vtable slot [3], 0x14012bff0)
// ============================================================
// Serializes the activeMask icon state. Simple: Component_Serialize base,
// then one uint via IO::Serializer(version 0). On load, writes back to +0xF0.

void StatusIconComponent::Persist(IO::Stream& stream) {
    // Base class serialize
    Component::Persist(stream);   // retail: CALL 0x1401189b0 (qualified)

    IO::Serializer ser(stream, 0);

    // Serialize the mask as uint (copy to local, serialize, write back on load)
    uint32_t iconVal = _geometry.activeMask;
    ser.Serialize(iconVal, nullptr);

    // On load (direction 0): write the value back to the field
    if (static_cast<int>(ser.GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        _geometry.activeMask = iconVal;
    }
}

// ============================================================
// StatusIconComponent::Startup (vtable slot [4], 0x14012bed0; the tree's
// StatusIconComponent_Serialize)
// ============================================================
// Reads sprite data from the StatusIconDescription, initializes the geometry:
// the SpriteSheet's sprite count, then Setup (0x1400aa440) with the icon size,
// the count, the constant-size flag and the sheet.
// ⚠ A LOAD never dispatches this slot — anything that must hold for a stream-restored
// component belongs in the ctor (StatusIconComponent::StatusIconComponent), not here.
// The icon's warning bits are raised/cleared by genuine events
// (WorkPlaceComponent_SetAccepting / _Attach for building warnings; CitizenComponent
// Update for citizen health icons) — no per-frame occupancy poll.
void StatusIconComponent::Startup() {
    StatusIconDescription* desc = GetStatusIconDescription(this);
    if (!desc) return;

    // The sprite count of the SpriteSheet at description +0x20
    UI::SpriteSheet* spriteSheet = desc->_spriteSheet;
    if (!spriteSheet) return;

    int spriteCount = spriteSheet->GetSpriteCount();

    // Setup(size = desc +0x28, count, constant = desc +0x30's low byte, sheet = desc +0x20)
    _geometry.Setup(desc->_scale, spriteCount, static_cast<uint8_t>(desc->_flag), spriteSheet);
}
// [7] TREE DIVERGENCE (retail 0x14012beb0, 15 B): the removed-from-world handler
// above (StatusIconComponent::OnRemove) — MarkForRemoval (0x1400c2ea0) dispatches
// it for capability bit 1 (0xc2 has it); it removes the icon's scene object so
// the renderer never holds freed geometry.
// ============================================================
// Standalone faithful port of StatusIconComponent::Tick (0x14012bbd0)
// ============================================================
// Manages Scene add/remove on the game-settings icon-visibility byte (settings+0xc),
// then positions the icon. Buildings take the MODEL-BOUNDS path (icon centred above
// the model's bounding box); the map-cell fallback mirrors the original for entities
// without a model. Verified line-by-line against the disassembly.
void StatusIconComponent::Tick(const System::Time* /*time*/) {
    GameSettings* settings = g_gameSettings;
    if (!settings || GameUpdateLoop::_singleton == nullptr) return;
    Graphics::Scene* scene = StatusIcon_Scene();
    uint8_t iconVisible = settings->statusIconEnable;

    int32_t sceneId = iconId;
    if (iconVisible == 0) {
        if (sceneId >= 0 && scene) {
            scene->RemoveObject((uint32_t)sceneId);
            iconId = -1;
        }
    } else if (sceneId < 0) {
        if (scene) {
            uint32_t id = scene->AddObject(&_geometry);
            iconId = (int32_t)id;
            SceneWatch_Note(id, &_geometry, "StatusIconComponent::Update");
        }
    }
    if (iconId < 0) return;

    // Icon opacity (settings+8 / 100) cached in the geometry's _opacity/_opacityPrev
    // (+0xa4/+0xa8); dirty bit 2 on change.
    float scaleVal = (float)(settings->statusIconOpacity) / 100.0f;
    if (scaleVal != _geometry._opacity) {
        _geometry._dirtyFlags |= 4u;
        _geometry._opacity = scaleVal;
        _geometry._opacityPrev = scaleVal;
    }

    ComponentEntity* entity = _entity.Get();

    // Resolve Model + MultiObject by NAME (the exe finders key off unfilled .rdata globals
    // c7a50/c7ac0 → null for every entity in standalone; see StatusIcon_FindCompByName).
    // The MODEL-BOUNDS path then resolves for citizens (model = CharacterComponent's sibling
    // ModelComponent, +0x158 = live move-tick position) AND faithfully preserves the original's
    // branch for buildings that carry a MultiObjectComponent (→ map-cell). Map fallback below
    // still uses the exe finder (its c79e0 global IS filled, so it works).
    // [ext.render_culling ICON_LASTONLY -- NON-RETAIL] the positioning below is pure presentation
    // (Geometry::SetTransform on the icon sprite); on intermediate speed iterations the
    // result is overwritten before anything draws it, so only the last iteration runs it.
    if (Ext::RenderCulling::IconLastOnly() && Ext::RenderCulling::RootOnlyNow()) {
        return;
    }
    Component* model = StatusIcon_FindCompByName(entity, ModelComponent::TypeId(), "ModelComponent");
    // [ext.render_culling ICON_CULL -- NON-RETAIL] an icon whose owner model is off-screen is not
    // repositioned; the tick is replayed (root-only flag clear) if the sim pauses.
    if (model && Ext::RenderCulling::IconCull() && Ext::RenderCulling::ModelCulled(static_cast<ModelComponent*>(model))) {   // found by ModelComponent::TypeId() (B10)
        Ext::RenderCulling::NoteDeferred(this, [](StatusIconComponent* o) {
            o->StatusIconComponent::Tick(nullptr);
        });
        return;
    }
    StatusIcon_SetIconTransform(this, entity, model);
}

// FIX icon_slide's placement (ShowAfterLoad / SetFlag, before AddObject): Tick's positioning
// tail alone -- no scene add/remove, no opacity, no PerfX shortcuts.
static void StatusIcon_PlaceIcon(StatusIconComponent* c) {
    ComponentEntity* entity = c->_entity.Get();
    Component* model = StatusIcon_FindCompByName(entity, ModelComponent::TypeId(), "ModelComponent");
    StatusIcon_SetIconTransform(c, entity, model);
}

// The positioning tail of Tick (0x14012bbd0), split out unchanged so FIX icon_slide can run
// it before AddObject: the icon's position (model bounds, else the map cell) and
// Geometry::SetTransform. `model` is the caller's ModelComponent lookup.
static void StatusIcon_SetIconTransform(StatusIconComponent* c, ComponentEntity* entity, Component* model) {
    StatusIconDescription* desc = GetStatusIconDescription(c);

    float pos[3];
    pos[0] = 0.0f;
    pos[1] = 0.0f;
    pos[2] = desc ? desc->_scale2 : 0.0f;   // base height offset

    Component* multi = StatusIcon_FindCompByName(entity, MultiObjectComponent::TypeId(), "MultiObjectComponent");

    if (multi == nullptr && model != nullptr) {
        // MODEL-BOUNDS path: the mesh's local bounds (model+0x5c = meshInstance+0x34);
        // size.z = max.z - min.z, on top of the model's translation.
        const ModelComponent* mdl = static_cast<const ModelComponent*>(model);
        const ::Box& bounds = mdl->meshInstance._localBounds;
        float sizeZ = bounds.max.z - bounds.min.z;
        pos[0] = mdl->transform.translation.x;
        pos[1] = mdl->transform.translation.y;
        pos[2] = mdl->transform.translation.z + sizeZ;
    } else {
        // MAP-CELL fallback.
        MapComponent* mc = entity->FindComponent<MapComponent>();   // 0x140004680
        if (mc) {
            Map* widthPtr = GameUpdateLoop::_singleton->m_game;
            int w = widthPtr->size;
            // The footprint is read as int16 (the tree's width; MapComponent.h declares
            // uint16 -- COMP-d's header, LISTED).
            int idx = ((int)static_cast<int16_t>(mc->tileY0) + ((int)static_cast<int16_t>(mc->tileY1) >> 1)) * w
                      + ((int)static_cast<int16_t>(mc->tileX1) >> 1) + (int)static_cast<int16_t>(mc->tileX0);
            float f = (float)(w >> 1) - 0.5f;
            float in[3]  = { (float)(idx % w) - f, (float)(idx / w) - f, 0.0f };
            float out[3] = { 0.0f, 0.0f, 0.0f };
            widthPtr->GetTerrainPos(out, in, 0, nullptr);   // 0x14009f3e0
            pos[0] += out[0];
            pos[1] += out[1];
            pos[2] += out[2];
        }
    }

    // Transform: {rotation (0,0,0,1), translation pos} — pure translation.
    Math::Transform xf;
    xf.translation = Math::Vector3(pos[0], pos[1], pos[2]);
    c->_geometry.SetTransform(xf);
}
// [11] 0x14012bf40 — the post-load re-show (ShowAfterLoad above).
void StatusIconComponent::Slot11() { ShowAfterLoad(); }

// create 0x1400388c0: MemAlloc(0x108, 8), then the ctor.
::Object::Base* StatusIconComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(StatusIconComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) StatusIconComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* StatusIconComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) StatusIconComponent());
}

DEFINE_EXE_TYPE(StatusIconComponent, "Component")
