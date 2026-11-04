// src/game/Component/decalcomponent.cpp
// Clean reimplementation of DecalComponent lifecycle methods.
//
// DecalComponent (0x38 bytes) manages ground decals for buildings (shadows, footprints).
// Fields: +0x28=decalState (int32), +0x2C=decalId (int32, -1=none), +0x30=decalScale (float, 1.0)
//
// Vtable at 0x140162fd0. Non-trivial overrides:
//   [3] Persist  (0x14011b820) -- serializes decalScale and decalState
//   [4] Startup  (0x14011b7d0) -- decalScale = DecalDescription::_initialAlpha
//   [6] OnAdded  (0x14011b560) -- builds the terrain decal (AddDecal)
//   [7] OnRemove (0x14011b510) -- releases the decal slot if decalId >= 0
//
// Decompiled from Application-x64.exe using Ghidra.

#include "Component/DecalComponent.h"
#include "Component/ModelComponent.h"   // B.3.8 CB1a (TypeId)
#include "Component/DecalDescription.h"   // B.3.8 CB1a (TypeId)
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/exe_constants.h"   // kRdata_* (B3R Leftovers: the .rdata constants)
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch A
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include "Object/factory_registration.h"   // ::Object::GetTypeIdByName (SA type-id fallback)
#include "Component/exe_bodies.h"
#include "Action/exe_bodies.h"
#include "Component/MapComponent.h"
#include "Entity/ComponentEntity.h"
#include "Game/WorldGeometry.h"
#include "Game/WorldGeometryElements.h"   // GeometryBatch (the _decals element)
#include <cstring>
#include "Player/exe_bodies.h"   // AddDecal 0x1400b0d80

// ============================================================
// Helpers, called directly: IsDerivedFrom (import slot 0x140152a98) is
// Object::Base::IsDerivedFrom; the terrain-decal remove FUN_1400b0d00 is
// DecalComponent_TerrainDecalRemove below.
// ============================================================

// Sub-callees of the terrain-decal-slot release (FUN_1400b0d00) were
// Collection::Array<T>::SetSize monomorphised into the exe (ENDGAME B.3.2
// step 2): FUN_14001ee40 stride 0x14 align 2 (Array<GeometryBatchVertex>::
// SetSize, the trait row: NO zero-tail write at all, but both call sites HERE
// pass newCount==0, so the tail loop
// never executes and no init policy is ever exercised: any correctly
// sized/aligned trivial element type gives byte-identical behaviour at
// count==0). FUN_14001c890 is the already-catalogued U16 SetSize (stride 2,
// align 2, zero tail — also never exercised at count==0). Both are converted
// below; the two typedefs and resolves are gone.

// ============================================================
// Reimpl of FUN_1400b0d00 (TerrainDecalSystem release one decal slot)
// ============================================================
// Shared terrain-decal helper (also called from RadiusUI/PlacementTool).
// The decal system at terrainDecalSys+0x1F0 is an array of 0x60-byte slots.
// If slot[decalId].id (+0x28) still equals decalId, mark it free (-1), clear
// the slot's owner ptr (+0), and shrink the two sub-lists embedded at +8 and
// +0x18 to zero length. 1:1 port of the exe body.
// WorldGeometry::RemoveDecal: the decals are WorldGeometry::_decals (+0x1F0),
// 0x60-byte GeometryBatch records (WorldGeometryElements.h).
void WorldGeometry::RemoveDecal(int decalId) {
    GeometryBatch* slot = _decals._data + decalId;   // +0x1F0
    if (slot->slot == decalId) {   // +0x28 id
        slot->slot = -1;           // +0x28 = 0xffffffff
        slot->material = nullptr;  // +0x00 qword
        // exe 0x14001ee40 (0x1400b0d4c) / 0x14001c890 (0x1400b0d61): the two typed
        // arrays' Collection::Array<T>::SetSize(0) (B.4 H5-P2).
        slot->vertices.SetSize(0);   // +0x08
        slot->indices.SetSize(0);    // +0x18
    }
}

// ============================================================
// Helper: the component's DecalDescription (IsDerivedFrom 0x1401c75b8, else null)
// ============================================================
// Retail calls IsDerivedFrom on [RCX+0x18] with no test (0x14011b641 / 0x14011b7ea).
static DecalDescription* GetDecalDescription(DecalComponent* self) {
    Description* obj = self->_description;
    bool derived = obj->IsDerivedFrom(DecalDescription::TypeId());
    return derived ? static_cast<DecalDescription*>(obj) : nullptr;
}

// ============================================================
// DecalComponent::Draw (vtable slot [6], 0x14011b560)
// ============================================================
// Real terrain-decal rendering (was a delegate to the exe body). Faithful 1:1 port of
// the 0x14011b560 disassembly: read the building footprint (MapComponent tile bbox +
// rotation), the model variant index (ModelComponent+0x164 → per-variant UV rect), and
// the DecalDescription (material list + tiled/uv params), then submit one decal to the
// terrain-decal system (gameWorld+0x110) via AddDecal (TradeWork_impl_0d80 @ b0d80).
// AddDecal fills a slot in the +0x1f0 array and tessellates it (ace40); the slot is then
// rendered every frame by GeomDraw_CollectDraw's +0x1f0 loop (geometry_draw.cpp). The
// resulting decalId is stored at self+0x2c so OnCreate can release it.
//
// This is the brown ground foundation under a StorageYard (and any building carrying a
// DecalDescription). Dispatched by ComponentEntity::OnAddedPass at
// EntityList::RegisterEntity (the ctor's flags 0xc3 carry bit 0), as retail.

// AddDecal (0x1400b0d80, tool_base_highlight.cpp; Player/exe_bodies.h) — 19-arg
// tile-slot manager + tessellate.

// Draw-path helpers, called directly: 0x140004680 FindComponent(MapComponent)
// (FloorComponent_FindResData), 0x140003f60 FindComponent(ModelComponent)
// (ActCNR_FindComponent_3f60), DAT_1401c3dd8 = g_pGameWorldSingleton.

// ============================================================================
// ENDGAME B.3.8 VIS — DecalComponent as a REAL class (DecalComponent.h).
//
// Before: a 48-slot DecalComponent_LiveVtable filled per create, Inert_ScalarDtor
// in slot 0 (only ~Internal), the ctor scalars patched in after the zero-fill.
// After: the compiler's 16-slot vtable and a real ctor/dtor chain (B38_RULES
// D1, D2); slot bodies are the ones the old table installed.
// ============================================================================
#include "Object/factory_registration.h"
#include "Component/DecalComponent.h"

// Building ground-decal rendering. A building's DecalDescription (e.g. StorageYard's
// brown dirt "foundation" -- StorageYard.rsc `decal` -> Terrain/TiledDecals/
// StorageYardMaterial.rsc) is painted onto the terrain by OnAdded, which AddDecal's it
// into the terrain-decal system at gameWorld+0x110. That slot array is walked and
// submitted every frame by GeomDraw_CollectDraw (geometry_draw.cpp), so the decal is
// BUILT ONCE, from OnAdded (row 1270: no retry, no load race), and rebuilt only by the
// slot-12 arms (the event-1 opaque ramp below, the scorch / quarry re-adds) and by
// slot 11 after a load.
// 0x14011b8f0: Component() is the CALL 0x1401188b0; the 0x140162fd0 stamp is
// the compiler's; then
//     c7 43 28 00000000   mov [rbx+0x28], 0        decalState = 0
//     c7 43 2c ffffffff   mov [rbx+0x2c], -1       decalId    = -1
//     c7 43 30 0000803f   mov [rbx+0x30], 1.0f     decalScale = 1.0
// and flags |= 0xc3. decalId -1 matters: 0 means "I own terrain decal slot 0",
// and decalId is NOT in the save stream (slot 3 serializes only decalScale and
// decalState), so a stream-restored decal with 0 would TerrainDecalRemove(sys, 0)
// at world teardown. See reference_ctor_scalars_in_startup_are_unset_after_load.
DecalComponent::DecalComponent()
    : decalState(0), decalId(-1), decalScale(1.0f), _pad34{} {
    OrFlagsInCtor(0xc3);
}

// [3] 0x14011b820 — decalScale/decalState round-trip here.
// ============================================================
// DecalComponent::Shutdown (vtable slot [3], 0x14011b820)
// ============================================================
// Serializes:
//   1. Component_Serialize base
//   2. Serializer(version=1): serialize decalScale (float at +0x30)
//   3. If version > 0: serialize decalState (int at +0x28)
void DecalComponent::Persist(IO::Stream& stream) {
    Component::Persist(stream);   // retail's direct CALL 0x1401189b0
    IO::Serializer ser(stream, 1);

    // Serialize decalScale (float at +0x30)
    ser.Serialize(decalScale, nullptr);

    // Version-gated: if version > 0, also serialize decalState, through the int door
    // (0x14011b882 CALL [0x140152ac8], Serialize(int&)).
    if (ser.GetVersion() > 0) {
        ser.Serialize(decalState, nullptr);
    }
}
// [4] 0x14011b7d0-0x14011b808: IsDerivedFrom on [RCX+0x18] (cell 0x1401c75b8), CMOVNZ,
// then `+0x30 = [desc+0x20]` with no test. Dispatched by ComponentEntity::
// StartupComponents from the description ctor (0x1400c38bc) and by Persist only for a
// component the save lacks (0x1400c3343): a loaded decal keeps its saved decalScale.
// So a new building's foundation paints at _initialAlpha (0.5, translucent) until the
// ClearArea state-2 event 1 (OnEvent below) sets 1.0.
void DecalComponent::Startup() {
    decalScale = GetDecalDescription(this)->_initialAlpha;
}
// [6] 0x14011b560 — Draw: build + submit the terrain decal.
//
// Retail tests nothing here but the ModelComponent (0x14011b60d CMP / JZ): the
// MapComponent (0x14011b5d6), the DecalDescription cast (0x14011b64b CMOVNZ, then
// [RSI+0x40] at 0x14011b64f), GameUpdateLoop+0x110 (0x14011b687) and the material
// array (0x14011b6f4) are all read unguarded; the only null test on the way is
// AddDecal's own material test (0x1400b0dbb). The tree's six early returns served
// the July per-frame build driver (2ac870693 / 85845b95b), which retried the build
// until it stopped bailing -- see known_port_issues row 1270.
void DecalComponent::OnAdded() {
    // CheckPtr(owner) then FindComponent(MapComponent) for the tile footprint.
    MapComponent* mapComp = _entity->FindComponent<MapComponent>();   // 0x140004680

    int minX   = static_cast<int16_t>(mapComp->tileX0);
    int minY   = static_cast<int16_t>(mapComp->tileY0);
    int width  = static_cast<int16_t>(mapComp->tileX1);
    int height = static_cast<int16_t>(mapComp->tileY1);
    uint32_t rotation = static_cast<uint32_t>(mapComp->extraField);

    // FindComponent(ModelComponent) (0x140003f60) → the mesh-group index (+0x164). This
    // is the per-building visual style StartTown randomises (ComponentEntity_impl_1fc0
    // in retail); it selects which atlas cell of the tiled-decal material this
    // building's foundation samples. Defaults to 0 if no ModelComponent is found.
    int variant = 0;
    ModelComponent* modelComp = _entity->FindComponent<ModelComponent>();
    if (modelComp != nullptr) {
        variant = modelComp->meshGroup;
    }

    // DecalDescription = IsDerivedFrom(_description, DecalDescription typeId) ? it : null.
    DecalDescription* decalDesc = GetDecalDescription(this);

    // Decal alpha: decalScale (+0x30) * 255 -> the vertex alpha. Startup seeds it from
    // _initialAlpha; OnEvent(1) sets 1.0 and re-adds.
    // Per-variant UV rect (defaults {0,0,uvScale,uvScale} when variant >= count).
    float uvScale = kRdata_14017214c;
    DecalUVRect uv = {0.0f, 0.0f, uvScale, uvScale};
    if (variant < decalDesc->_uvRectCount) {
        uv = decalDesc->_uvRects[variant];
    }

    WorldGeometry* decalSys = GameUpdateLoop::_singleton->m_worldGeometry;

    // material = DecalDescription material-array[decalState].
    Graphics::MaterialInstance* material =
        DecalStateMaterial(decalDesc->_materials, decalDesc->_materialCount, decalState);

    bool     tiledFlag = decalDesc->_tiled;
    bool     p19       = decalDesc->_flagB;
    float    p10       = decalDesc->_sizeB;
    const uint8_t alpha = DecalAlphaByte(decalScale);   // DAT_14017222c = 255.0f

    // AddDecal 0x1400b0d80 (was its TradeWork_impl_0d80 forwarder; its dead param_18 is gone).
    int id = decalSys->AddDecal(minX, minY,
        width - 1 + minX, height - 1 + minY,
        alpha, rotation,
        material,
        tiledFlag, p10,
        uv.u0, uv.v0, uv.u1, uv.v1,
        '\0', 0, 0, p19);
    decalId = id;
}
// [7] 0x14011b510 — OnCreate: release the decal slot.
// ============================================================
// DecalComponent::OnCreate (vtable slot [7], 0x14011b510)
// ============================================================
// If decalId >= 0, removes the decal from terrain, resets decalId to -1.
void DecalComponent::OnRemove() {
    if (decalId >= 0) {
        // Remove decal from terrain decal subsystem
        if (GameUpdateLoop::_singleton != nullptr) {
            WorldGeometry* terrainDecalSys = GameUpdateLoop::_singleton->m_worldGeometry;
            if (terrainDecalSys)
                terrainDecalSys->RemoveDecal(decalId);   // 0x1400b0d00
        }
    }
    decalId = -1;
}
// [11] 0x14011b810 (7 B): `MOV RAX,[RCX]; JMP [RAX+0x30]` -- slot 11 IS OnAdded.
// EntityList::PostLoad 0x1400c4730 calls slot 11 on every component of every loaded
// entity, so a loaded building rebuilds its foundation decal from the saved
// decalScale / decalState (decalId is not in the stream; the ctor's -1 stands).
// Row 1271.
void DecalComponent::Slot11() {
    OnAdded();
}

// [12] 0x14011b760. Retail's two arms (disassembly 0x14011b769-0x14011b7c5):
//   event 1 (0x14011b769): when decalScale != 1.0f (UCOMISS vs 0x14017214c, the
//     parity jump counts NaN as "not equal") and a decal is placed (+0x2c >= 0):
//     OnRemove ([RAX+0x38]), decalScale = 1.0f, tail OnAdded ([RAX+0x30]).
//   event 5 (0x14011b7a0): when a decal is placed: OnRemove, tail OnAdded (the
//     rebuild after FinishClear 0x140118270 reset the terrain under it).
// Every other event is a RET. Retail's only event-1 sender is
// ClearAreaComponent::State2Enter 0x1401177f0 (the site is cleared, the foundation
// claims its cells), so the translucent planned footprint turns opaque there;
// event 5's only sender is FinishClear 0x140118309 (the 0x1400c2b70 xrefs: every
// other broadcast passes 0, 2, 3 or 4).
void DecalComponent::OnEvent(int eventId) {
    if (eventId == 1) {
        if (decalScale == 1.0f) return;   // kRdata_14017214c
        if (decalId < 0) return;
        OnRemove();
        decalScale = 1.0f;
        OnAdded();
        return;
    }
    if (eventId == 5) {
        if (decalId < 0) return;
        OnRemove();
        OnAdded();
    }
}

// create 0x1400337c0: MemAlloc(0x38, 8), then the ctor.
::Object::Base* DecalComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(DecalComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) DecalComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* DecalComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) DecalComponent());
}

DEFINE_EXE_TYPE(DecalComponent, "Component")
