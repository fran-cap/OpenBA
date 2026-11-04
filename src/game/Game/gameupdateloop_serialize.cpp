// src/game/Game/gameupdateloop_serialize.cpp
// Save/load wave 2026-08-27 — the SaveRestore keystone.
//
// GameUpdateLoop vtable +0x38 SaveRestore (exe 0x1400959c0) is what the engine
// calls in BOTH directions:
//   SAVE: Save::SaveLoad::UpdateSave (Runtime 0x1800c2a80, state 1) does
//         currentLoop->vtbl[+0x38](loop, 1, savedState, &out), then Persistence
//         async-writes <Documents>/Banished/Save/<name>.sav.
//   LOAD: retail GameUpdateLoop::Load (0x140096b00) calls its own +0x38 slot at
//         0x14009754c with save=0 when m_savedState (+0x58) is non-null.
// Until this file, the standalone slot was a no-op stub — a save completed
// "successfully" but wrote a 79KB file (metadata + screenshot, no world) vs
// retail's ~3.4MB.
//
// Ported here:
//   0x1400959c0  SaveRestore wrapper — CompressedMemoryStream around 95a90;
//                load: SavedState::Retrieve → Open; save: Create → Close →
//                SavedState::Store.
//   0x140095a90  master GameUpdateLoop serializer (~20 subsystem sections).
//   0x1400952c0  event-log serialize (m_eventNotifier +0x138; 0x28-stride
//                entries {uint,String,5 ints}, capped at the last 200).
//   0x1400199a0  event-log array resize (helper of 952c0).
//   0x140093c90  weather-sim serialize (m_weather +0x130; env ExternalPtr,
//                params, per-effect-slot active flag + 2 floats; on load
//                re-creates active effects via WeatherEffect_SetEnabled).
//   0x140102920  EntityUI serialize (m_toolManager +0x120: paused/speed/times,
//                goal-ref tree, IBP-keyed AVL, saved camera views 1-5).
//   0x14002e2e0  goal-ref tree serialize (helper of 102920).
//   0x14002e470  IBP-keyed AVL serialize (helper of 102920).
//   0x14002e000  camera-view array resize, stride 0x1c (helper of 102920).
//   0x1400a57f0  minimap serialize (m_imageCapture +0x148: two uint32 pixel
//                arrays + width; on load re-creates GPU buffers + uploads —
//                delegated to Minimap_CreateAndUpload which ports that tail).
//   0x1400c4730  EntityList post-load pass (per-component vcall +0x58, entity
//                backptr, update-list enrolment via DynArray8_ResizeFill).
//
// Ported elsewhere (parallel wave lanes, called by name since B3R):
//   0x14008f760/0x14008f580  CitizenBehavior serialize + post-pass (+0xF0)
//   0x14009f100              game map-grid serialize (+0xF8)
//   0x1400f9240              goal/tutorial-mgr serialize (+0x118)
// Already ported before this wave:
//   0x1400194a0/0x140019ec0/0x14001a0c0 (config ExternalPtrs), 0x1400988c0
//   (camera/PlayerController), 0x1400c44a0 (EntityList::Serialize),
//   0x1400b04d0 (WorldGeometry).

#include "err_line.h"   // ErrLine (HITCH_POSTDRAIN): the HITCH_SAVE harness lines
#include "stress_harness.h"   // g_verifyOn / g_pfProfOn: the HITCH_SAVE memo check and lines
#include "Game/GameUpdateLoop.h"   // caller batch CB6: SaveRestore, the named fields
#include "Game/serialize_scope.h"   // SerializeAll's two halves (ext.background_save)
#include "Game/Map.h"
#include "Entity/CitizenBehavior.h"   // B4 GAME-a: m_citizenList->Serialize / PostLoad
#include "Entity/EntityList.h"
#include "Game/PlayerToolManager.h"
#include "Game/Minimap.h"
#include "Game/minimap_bounds.h"   // review S-009: the counts Minimap::Serialize accepts on load
#include "Game/WeatherSimulation.h"
#include "Game/WeatherParticleGeometry.h"   // the effect slots' class
#include "Game/EventLog.h"            // B4 GAME-a: the pointees the serializer reads by name
#include "Game/Calendar.h"   // m_dayCounter's class (B.4 GAME-a)
#include "Entity/ComponentEntity.h"   // caller batch CB6: TypeId()
#include "Component/Component.h"      // caller batch CB6: slot 11
#include "math/Math.h"
#include "object/TypeInfo.h"
#include "graphics/Camera.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "options.h"
#include "system/String.h"
#include "collection/Collection.h"
#include <new>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch B
#include "io/Stream.h"       // ENDGAME B.3.9 IO: IO::CompressedMemoryStream
#include "graphics/AnimationManager.h"   // ENDGAME B.3.5 Video/Graphics caller batch
#include "system/Value.h"      // ENDGAME B.3.4b System caller batch (Random/Time/Color)
#include "ui/UserInterface.h"  // ENDGAME B.3.6 UI callers (K4)
#include "achievement/AchievementManager.h"   // ENDGAME B.3.7 callers (K2)
#include "save/SaveLoad.h"                    // ENDGAME B.3.7 callers (K2)
#include "audio/Audio.h"                      // ENDGAME B.3.7 callers (K4)
#include "object/External.h"   // ENDGAME B.3.9 SS2: Object::ExternalFactory::_singleton
#include "Game/exe_bodies.h"
#include "Action/exe_bodies.h"         // ActHunt_DynArraySetCount_3110
#include "Dialog/exe_bodies.h"         // Avl38SetInsert (0x140014890)
#include "Player/exe_bodies.h"         // Avl38Retrace (0x140025920)
#include "system/Filename.h"
#include "Game/MapGenerator.h"   // B8 RT2: ::Object::SerializeExternal<MapGenerator> (the downcast needs it complete)
#include "Component/ComponentDescription.h"   // B8 RT2: ::Object::SerializeExternal<ComponentDescription> (the downcast needs it complete)
#include "Component/EnvironmentDescription.h"   // B8 RT2: ::Object::SerializeExternal<EnvironmentDescription> (the downcast needs it complete)
#include "Game/ClimateGenerator.h"   // B8 RT2: ::Object::SerializeExternal<ClimateGenerator> (the downcast needs it complete)
#include "Game/TerrainGenerator.h"   // B8 RT2: ::Object::SerializeExternal<TerrainGenerator> (the downcast needs it complete)
#include "Game/WorkScheduler.h"   // WorkScheduler::Serialize (0x1400f9240)
#include "Game/WorldGeometry.h"   // WorldGeometry::Serialize (0x1400b04d0)
#include "Game/PlayerController.h"   // PlayerController::Serialize (0x1400988c0)
#include "rt_crt.h"   // rtcrt::StrICmp, retail's _stricmp (D.4 Q9)
#include "system/Error.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSAVE.md): a bad B / C manifest is retail's fatal error box
#include "ext/registry.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSAVE.md): the B / C lock from the save
#include "ext/savetail.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSAVE.md): the block-1 tail manifest
#include "ext/async_save_compress/async_save_compress.h"   // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_SAVE.md): ext.async_save_compress, the save arm
#include "ext/rock_seat/rock_seat.h"   // FIX rock_seat (NOT RETAIL, docs/extensions/handins/ROCKSEAT.md): the post-load seat
#include <string>
#include <vector>
#include "load_time.h"   // [LoadTime] stamps (NOT RETAIL, print-only; block LOADSTAMP)

// src/game/Game/minimap_upload.cpp — ports FUN_1400a4e80 + the CopyToTexture tail.
extern "C" void Minimap_CreateAndUpload(Minimap* mapObj);
// src/game/Game/weather_particles.cpp — ports FUN_1400942c0 (enable half; the
// ambient-audio entity spawn is not ported there yet — see WeatherSim_Serialize).
extern "C" void WeatherEffect_SetEnabled(WeatherSimulation* sim, int slot, int enable);

namespace {

// B3R: every callee is called by name. The Runtime exports are members of
// Object::ExternalFactory (EnableReferenceMode, SerializeReferenceTable,
// GetPathForReferenceId, FindOrLoad) and Object::InternalFactory
// (ResetPersistState, ClearPersistState). The exe helpers:
//   0x1400194a0 / 0x140019ec0 / 0x14001a0c0 / 0x1400192a0
//       Reimpl_ExternalPtr_Serialize_{ClimateGenerator,MapGenerator,TerrainGenerator,
//       EnvironmentDesc}                                          (Game/exe_bodies.h)
//   0x1400988c0 Reimpl_Transform_SerializeWrapper (PlayerController, +0xE0)
//   0x1400c44a0 EntityList_Serialize (+0xE8)                   (Entity/entitylist_funcs.h)
//   0x1400b04d0 Reimpl_TradeWork_impl_04d0 (WorldGeometry, +0x110)
//   0x14008f760 / 0x14008f580 CitizenBehavior::Serialize / PostLoad (+0xF0)
//   0x14009f100 GameGrid_Serialize_9f100 (+0xF8), 0x1400f9240 Goals_Serialize_f9240 (+0x118)
//   0x140003110 ActHunt_DynArraySetCount_3110                  (Action/exe_bodies.h)
//   0x1400021d0 Reimpl_Array_Resize_Stride04, 0x140002340 Reimpl_ExternalPtr_Serialize_StringTable
//   0x140014890 Avl38SetInsert (PinnedToolNode wrapper)        (Dialog/exe_bodies.h)
//   0x140025920 Avl38Retrace                                   (Player/exe_bodies.h)
// The "[GUL::Serialize] resolved:" stderr line went with the caches.
//
// ENDGAME B.3.9 IO (W2): the six IO::CompressedMemoryStream door typedefs and
// their resolved statics are gone -- SaveRestore and the roundtrip check
// build the real class (src/engine/io/Stream.h) and call its members.

inline bool IsSaving(const IO::Stream* stream) {
    return stream->GetDirection() == IO::kWriting;   // Stream +0x08 == 1
}

} // namespace

// ============================================================
// 0x1400199a0 — event-log array resize (stride 0x28)
// ============================================================
// hdr = { void* data; int count; int cap }. Element: { uint id; String text;
// int a,b,c,d,e }. Grow reallocs (count+1 growth triples the cap, matching the
// generic resize family); shrink String-dtors the tail; realloc copies via
// String copy-ctor and dtors+frees the old block. New elements init to
// { -1, null-String, 0, 0, -1, -1, -1 }.
//
// ENDGAME B.3.2 step 2: this body and CamViewArray_Resize_e000 below WERE
// hand-written copies of `rtcoll::SetSizeCore` (src/engine/collection/
// array.cpp); each is now one `Collection::Array<>::SetSize` over its own
// `ArraySpec` row. Compared arm for arm: exact-equal early return, `_size`
// stored before the capacity test, treble-on-single-append-from-non-empty,
// copy-construct the preserved prefix only when the block moved, init the new
// tail, destroy the truncated tail in place / the whole old buffer on a
// realloc, then free. The old realloc arm was gated on `oldData != null`;
// SetSizeCore runs destroy over `oldSize` elements (zero when the buffer was
// never allocated) and hands the null to the null-tolerant 0x1800c7ad0 —
// same effect. The one deliberate difference: the allocation now goes
// through the LOCKING static `MemAlloc` (0x1800c7a50) instead of the unlocked
// instance `Allocate` reached through `fn_MemAlloc` — same allocator, same
// heap, plus the critical section retail's own Collection code takes.
namespace {

// The event entry's row is EventLogRows::kSpec_EventEntry (eventlog.cpp):
// one element for the grow 0x140019b70, this resize and ~Array 0x14000a8a0.

// (The camera-view row, 0x14002e000, is PlayerToolManager::cameraViews' own:
// PlayerToolManagerRows::kSpec_CamView, PlayerToolManager.h.)

}  // namespace

// ============================================================
// 0x1400952c0 — event-log serialize (GUL+0x138 object)
// ============================================================
// Layout: +0x10 bool[6] filter flags; +0x18 {data,count,cap} entry array
// (stride 0x28). Version-1 section; entries capped at the LAST 200.
void EventLog::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 1);
    ser.Serialize(6, field_0x10, nullptr);
    if (ser.GetVersion() > 0) {
        int total = entries.GetSize();
        int capped = total > 200 ? 200 : total;
        int skip = total - capped;
        if (skip < 0) skip = 0;
        ser.Serialize(capped, nullptr);
        if (!IsSaving(&stream)) {
            entries.SetSize(capped);   // the resize, 0x1400199a0
        }
        for (int i = skip; i < entries.GetSize(); i++) {
            EventLogEntry& e = entries[i];
            // Retail 0x14009538a CALL [0x140152a50] = Serialize(unsigned int&): the colour's own
            // type (B6 row 16; the tree used the int overload through a cast).
            ser.Serialize(e.color, nullptr);
            e.text.Serialize(ser);
            ser.Serialize(e.eventType, nullptr);
            ser.Serialize(e.severity, nullptr);
            ser.Serialize(e.tileIndex, nullptr);
            ser.Serialize(e.iconIndex, nullptr);
            ser.Serialize(e.popup, nullptr);
        }
    }
}

// ============================================================
// 0x140093c90 — weather-sim serialize (GUL+0x130 object, 0x78 bytes)
// ============================================================
// Layout: +0x00 EnvironmentDescription ExternalPtr; +0x08 ExternalPtr (config,
// serialized via the already-ported 0x1400194a0); +0x10/+0x14 ints; +0x18
// float; +0x1c Vector4; +0x2c,+0x30,+0x44,+0x48 floats; +0x4c int; +0x50
// {geo*}[count at +0x58] active-effect slots. On load an active slot is
// re-created via the ported WeatherEffect_SetEnabled(sim, i, 1), which also
// spawns the effect's ambient-audio entity, as retail's 0x1400942c0 does.
// Then each active geo's +0xc4 (intensity) and +0xc0 floats round-trip.
void WeatherSimulation::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);

    ::Object::SerializeExternal<EnvironmentDescription>(ser, environment);   // 0x1400192a0
    ::Object::SerializeExternal<ClimateGenerator>(ser, climate);      // 0x1400194a0

    ser.Serialize(field_0x10, nullptr);
    ser.Serialize(field_0x14, nullptr);
    ser.Serialize(temperature, nullptr);
    // ?Serialize@Vector4@Math@@QEAAXAEAVSerializer@IO@@@Z  retail 0x18001d360
    field_0x1C.Serialize(ser);
    ser.Serialize(field_0x2C, nullptr);
    ser.Serialize(field_0x30, nullptr);
    ser.Serialize(field_0x44, nullptr);
    ser.Serialize(field_0x48, nullptr);
    ser.Serialize(seasonStep, nullptr);

    int slotCount = effects.GetSize();
    for (int i = 0; i < slotCount; i++) {
        Graphics::Geometry** slot = &effects[i];
        bool active = (*slot != nullptr);
        ser.Serialize(active, nullptr);
        if (active) {
            if (!IsSaving(&stream)) {
                WeatherEffect_SetEnabled(this, i, 1);
            }
            // Every effect slot holds a WeatherParticleGeometry
            // (WeatherEffect_SetEnabled builds it); its +0xC4 / +0xC0 floats.
            WeatherParticleGeometry* geo = static_cast<WeatherParticleGeometry*>(*slot);
            ser.Serialize(geo->_fadeDirection, nullptr);   // +0xC4
            ser.Serialize(geo->_intensity, nullptr);       // +0xC0
        }
    }
}

// ============================================================
// 0x14002e000 — camera-view array resize (stride 0x1c, align 4)
// ============================================================
// hdr = { void* data; int count; int cap }. Element: { Vector3 pos; float a;
// float b; float c; bool used } (0x1c bytes). Retail body is a 4x-unrolled
// copy loop — ported as a plain loop with identical semantics.
// (Its body is PlayerToolManager::cameraViews.SetSize since B.4 X6-GAME-A.)

// ============================================================
// 0x14002e2e0 — goal-ref tree serialize (EntityUI +0x50)
// ============================================================
// Tree node (0x30+): +0x00 ExternalPtr-ish component ref (serialized via the
// ported 0x140002340), +0x08 int, +0x0c int, +0x10 bool, +0x18 parent,
// +0x20 left, +0x28 right. Count at tree hdr +0x8. On save: in-order walk
// serializing each node. On load: read count entries and insert via the
// ported 0x140014890.
// The set is PlayerToolManager's {root +0x50, count +0x58}: `root` is its
// header (the count is the int at root + 8, read as the header's).
static void GoalRefTree_Serialize_e2e0(AvlTreeHead<PinnedToolNode>* set,
                                       IO::Serializer* serializer) {
    PinnedToolNode** root = &set->root;
    int count = set->count;
    serializer->Serialize(count, nullptr);

    if (static_cast<int>(serializer->GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        // LOAD: read `count` records, insert each
        for (int i = 0; i < count; i++) {
            // The record's +0x10 byte is read through the bool overload, as
            // before (B.4 Y GAMEB: the record is the set's element type now).
            PinnedToolEntry rec;
            bool used = false;
            rec.key = nullptr; rec.x = 0; rec.y = 0;
            ::Object::SerializeExternal<ComponentDescription>(*serializer, rec.key);   // 0x140002340
            serializer->Serialize(rec.x, nullptr);
            serializer->Serialize(rec.y, nullptr);
            serializer->Serialize(used, nullptr);
            rec.active = used;
            Avl38SetInsert(set, rec);   // 0x140014890
        }
    } else {
        // SAVE: leftmost-first in-order walk
        PinnedToolNode* node = *root;
        if (!node) { return; }
        PinnedToolNode* cur = node;
        while (cur->left) cur = cur->left;
        while (cur) {
            ::Object::SerializeExternal<ComponentDescription>(*serializer, cur->key);   // 0x140002340
            serializer->Serialize(cur->x, nullptr);
            serializer->Serialize(cur->y, nullptr);
            serializer->Serialize(cur->active, nullptr);
            // in-order successor
            PinnedToolNode* next = cur->right;
            if (next) {
                while (next->left) next = next->left;
                cur = next;
            } else {
                PinnedToolNode* parent = cur->parent;
                while (parent && parent->right == cur) {
                    cur = parent;
                    parent = parent->parent;
                }
                cur = parent;
            }
        }
    }
}

// ============================================================
// 0x14002e470 — IBP-keyed AVL serialize (EntityUI +0x60)
// ============================================================
// Node (0x38): +0x00 InternalBasePtr key (a ComponentEntity), +0x08 int,
// +0x0c int, +0x10 bool, +0x18 parent, +0x20 left, +0x28 right, +0x30 balance.
// Count at hdr +0x8. Load inserts by raw pointer order (CheckPtr'd), then
// rebalances via the ported 0x140025920.
// The set is PlayerToolManager's {root +0x60, count +0x68} (see above).
static void IbpTree_Serialize_e470(TrackedEntityNode** root, int32_t* countField,
                                   IO::Serializer* serializer) {
    const uint32_t s_tidCompEnt = ComponentEntity::TypeId();   // caller batch CB6: was a lazy by-name cache

    int count = *countField;
    serializer->Serialize(count, nullptr);

    if (static_cast<int>(serializer->GetDirection()) == 0) {   // IO::Serializer::GetDirection, retail 0x180006210
        // LOAD
        for (int i = 0; i < count; i++) {
            Object::InternalPtr<ComponentEntity> keyPtr;   // the key's InternalPtr (null)
            int a = 0, b = 0;
            bool used = false;
            keyPtr.Object::InternalBasePtr::Serialize(*serializer);
            // NOT RETAIL (F-143): null and type tests on the held Internal*; the
            // ComponentEntity* is taken only once they pass. The answer is the same.
            ComponentEntity* key = nullptr;
            if (!keyPtr.IsNull() && ::Object::IsDerivedFromNullSafe(keyPtr, s_tidCompEnt))   // 0x14002e4dd: no type-id test (row 1164)
                key = keyPtr.Get();
            serializer->Serialize(a, nullptr);
            serializer->Serialize(b, nullptr);
            serializer->Serialize(used, nullptr);

            auto makeNode = [&](TrackedEntityNode* parent) -> TrackedEntityNode* {
                TrackedEntityNode* n = static_cast<TrackedEntityNode*>(System::MemoryAllocator::MemAlloc(0x38, 8));
                if (!n) return nullptr;
                n->key = key;
                n->x = a;
                n->y = b;
                n->active = used ? 1 : 0;
                n->parent = parent;
                n->left = nullptr;
                n->right = nullptr;
                n->balance = 0;               // +0x30 / +0x32: one dword store of 0
                n->height = 0;
                return n;
            };

            TrackedEntityNode* rootNode = *root;
            if (!rootNode) {
                *root = makeNode(nullptr);
                *countField += 1;
                continue;
            }
            TrackedEntityNode* cur = rootNode;
            bool inserted = false;
            bool duplicate = false;
            TrackedEntityNode* newNode = nullptr;
            while (!inserted) {
                ComponentEntity* curKey = cur->key.Get();
                if (key < curKey) {
                    TrackedEntityNode* l = cur->left;
                    if (!l) {
                        newNode = makeNode(cur);
                        cur->left = newNode;
                        inserted = true;
                    } else cur = l;
                } else if (key > curKey) {
                    TrackedEntityNode* r = cur->right;
                    if (!r) {
                        newNode = makeNode(cur);
                        cur->right = newNode;
                        inserted = true;
                    } else cur = r;
                } else { duplicate = true; break; }
            }
            if (duplicate) continue;
            TrackedEntityNode* fixupNode = newNode;
            Avl38Retrace(root, &fixupNode);   // 0x140025920
            *countField += 1;
        }
    } else {
        // SAVE: in-order walk
        TrackedEntityNode* cur = *root;
        if (!cur) return;
        while (cur->left) cur = cur->left;
        while (cur) {
            cur->key.InternalBasePtr::Serialize(*serializer);   // InternalBasePtr::Serialize (0x14002e470)
            if (!cur->key.IsNull() && !::Object::IsDerivedFromNullSafe(cur->key, s_tidCompEnt))   // 0x14002e6cf: no type-id test (row 1164)
                cur->key = nullptr;
            serializer->Serialize(cur->x, nullptr);
            serializer->Serialize(cur->y, nullptr);
            serializer->Serialize(cur->active, nullptr);
            TrackedEntityNode* next = cur->right;
            if (next) {
                while (next->left) next = next->left;
                cur = next;
            } else {
                TrackedEntityNode* parent = cur->parent;
                while (parent && parent->right == cur) {
                    cur = parent;
                    parent = parent->parent;
                }
                cur = parent;
            }
        }
    }
}

// ============================================================
// 0x140102920 — EntityUI serialize (GUL+0x120 object, 0xB0 bytes)
// ============================================================
// Layout: +0x30 bool paused; +0x34 int speed; +0x3c/+0x40/+0x44 floats;
// +0x50 goal-ref tree; +0x60 IBP tree (v>0); +0x70 {data,count,cap} saved
// camera views, stride 0x1c: { Vector3 pos; float a; float b; float c;
// bool used } (v>1).
void PlayerToolManager::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 2);
    ser.Serialize(paused, nullptr);
    ser.Serialize(field_0x34, nullptr);
    ser.Serialize(field_0x3C, nullptr);
    ser.Serialize(field_0x40, nullptr);
    ser.Serialize(field_0x44, nullptr);
    GoalRefTree_Serialize_e2e0(&field_0x50, &ser);
    if (ser.GetVersion() > 0) {
        IbpTree_Serialize_e470(&field_0x60.root, &field_0x60.count, &ser);
    }
    if (ser.GetVersion() > 1) {
        int count = cameraViews.GetSize();
        ser.Serialize(count, nullptr);
        if (!IsSaving(&stream)) {
            cameraViews.SetSize(count);   // 0x14002e000
        }
        // Retail 0x140102a19..0x140102a25: MOV EAX,[count] / TEST EAX,EAX / JLE past the
        // loop / MOV EDI,EAX: a signed count, so a save's count <= 0 runs no element.
        for (int i = 0; i < count; i++) {
            PlayerCameraView& e = cameraViews[i];
            // ?Serialize@Vector3@Math@@QEAAXAEAVSerializer@IO@@@Z  retail 0x1800150e0
            e.position.Serialize(ser);
            ser.Serialize(e.field_0x0C, nullptr);
            ser.Serialize(e.field_0x10, nullptr);
            ser.Serialize(e.field_0x14, nullptr);
            ser.Serialize(e.used, nullptr);
        }
    }
}

// ============================================================
// 0x1400a57f0 — minimap serialize (GUL+0x148 object, 0x38 bytes)
// ============================================================
// Layout: +0x04 int width; +0x08/+0x10 ImageBuffer*; +0x18 {data,count,cap}
// terrain pixels; +0x28 {data,count,cap} overlay pixels. Version 0. On load,
// the GPU buffers are (re)created and both arrays uploaded — that tail is the
// already-ported Minimap_CreateAndUpload (FUN_1400a4e80 + Lock/Copy/Unlock).
void Minimap::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);

    // overlay array (+0x28) first — matches the retail order
    int n = overlayPixels._size;
    ser.Serialize(n, nullptr);
    if (!IsSaving(&stream)) overlayPixels.SetSize(n);   // 0x1400021d0
    ser.Serialize(n, overlayPixels._data, nullptr);

    // terrain array (+0x18)
    n = terrainPixels._size;
    ser.Serialize(n, nullptr);
    if (!IsSaving(&stream)) terrainPixels.SetSize(n);   // 0x1400021d0
    ser.Serialize(n, terrainPixels._data, nullptr);

    ser.Serialize(size, nullptr);

    // NOT RETAIL (review S-009): retail trusts the three counts; arrays that do not hold
    // size * size pixels each are refused before the upload reads them
    // (MinimapBounds::PixelsCoverSize), through retail's own fatal error box, as Map::Serialize
    // refuses a damaged map (review F-026).
    if (!IsSaving(&stream) && !MinimapBounds::PixelsCoverSize(size, overlayPixels._size, terrainPixels._size)) {
        fprintf(stderr, "[Minimap::Serialize] FATAL: edge %d, %d overlay / %d terrain pixels\n", size,
                overlayPixels._size, terrainPixels._size);
        fflush(stderr);
        System::Error::SetText("This save's minimap is damaged and cannot be loaded.", true);
        System::Error::Display();   // the box; _stopApplication then exits the process
        return;
    }

    if (!IsSaving(&stream)) {
        Minimap_CreateAndUpload(this);
    }
}

// ============================================================
// 0x1400c4730 — EntityList post-load pass
// ============================================================
// LOAD ONLY. For each entity: vcall +0x58 (post-load fixup) on every
// component, write the EntityList backptr at entity+0x10, then enrol the
// entity in the two update lists (+0x88 per-frame, +0x98 per-tick) when its
// flag word +0x40 has bit 2 / bit 4 set (DynArray8_ResizeFill append).
static void EntityList_PostLoad_c4730(EntityList* el, IO::Stream* stream) {
    if (stream->GetDirection() != IO::kReading) return;   // save direction: no-op

    int count = el->masterArray._size;
    for (int i = 0; i < count; i++) {
        Object::InternalPtr<ComponentEntity>* entSlot = &el->masterArray._data[i];
        ComponentEntity* ent = entSlot->Get();

        int compCount = ent->_components._size;
        for (int j = 0; j < compCount; j++) {
            Component* comp = ent->_components[j].Get();
            // Caller batch CB6: slot 11 (vtable +0x58) of an entity component-array
            // element (a proved Component).
            comp->Slot11();
        }

        (*entSlot)->_entityList = el;

        ComponentEntity* e = entSlot->Get();
        if (e->_entityList != nullptr) {
            // Retail re-reads the owner it has just stored.
            EntityList* owner = e->_entityList;
            uint32_t flags = e->_entityFlags;
            if (flags & 4) {
                ::Object::InternalPtr<ComponentEntity> ibp;   // the zeroed slot the placement ctor fills
                new (&ibp) ::Object::InternalBasePtr(e);   // ComponentEntity is-an Object::Internal (its one base)
                owner->stagingUpdate.SetSizeFill(owner->stagingUpdate._size + 1, &ibp);   // 0x140003110
            }
            if (flags & 0x10) {
                ::Object::InternalPtr<ComponentEntity> ibp;   // the zeroed slot the placement ctor fills
                new (&ibp) ::Object::InternalBasePtr(e);   // ComponentEntity is-an Object::Internal (its one base)
                owner->stagingPostUpdate.SetSizeFill(owner->stagingPostUpdate._size + 1, &ibp);   // 0x140003110
            }
        }
    }
}

// 0x140021ba0 -- the EntityList bucket-tree serializer is IO::Serializer::Serialize<UpdateBucket>
// (src/engine/io/Serializer.h), MSVC's clone at n = 5; its element step is
// Collection::Array<T>::Serialize (Collection.h). EntityList::Serialize calls it (B9 RT2).

// ============================================================
// 0x140095a90 — master GameUpdateLoop serializer
// ============================================================

// NOTE: a diagnostic mirror of Object::InternalBasePtr::Serialize used to live
// here. It established that the achievement IBP at self+0x150 is ABSENT in the
// tutorial stream (present=0), so retail returns early and that IBP is NOT the
// one that faulted -- the real desync was a skipped GoalInstance::Persist (see
// standalone/reg_goal.cpp). Removed once standalone/persistobject_probe.cpp
// could report the same thing by detouring PersistObject itself, which does not
// require standing in for any retail code.

// B4 GAME-a: a private member (retail's non-virtual 0x140095a90, called by
// SaveRestore 0x1400959c0 with this and the stream; R8 / R9 are never read).
// NOT RETAIL as two functions (ext.background_save, docs/extensions/handins/HITCH_SAVESNAP.md):
// retail's body is one straight line. It is cut after PlayerController::Serialize, the last
// part a camera-only frame writes, so ext.background_save can run the head on the main
// thread and the body on a worker. SerializeAll runs both halves back to back: the faithful
// sequence, statement for statement, with the version-3 Serializer living in `scope` across
// the cut where it was a block-scoped local.
void GameUpdateLoop::SerializeAll(IO::Stream& stream) {
    SerializeScope scope;
    SerializeAllHead(stream, scope);
    SerializeAllBody(stream, scope);
}

void GameUpdateLoop::SerializeAllHead(IO::Stream& stream, SerializeScope& scope) {
    ::Object::ExternalFactory* extFactory = ::Object::ExternalFactory::_singleton;
    ::Object::InternalFactory* intFactory = ::Object::InternalFactory::_singleton;

    const char* const serDir = IsSaving(&stream) ? "save" : "load";   // NOT RETAIL: print-only (block LOADSTAMP)
    scope.serDir = serDir;
    LoadTime::Stamp("ser.enter", serDir);   // NOT RETAIL: print-only
    fprintf(stderr, "[GUL::Serialize] %s ENTER self=%p stream=%p\n",
            IsSaving(&stream) ? "SAVE" : "LOAD", static_cast<void*>(this), static_cast<void*>(&stream));
    fflush(stderr);

    extFactory->EnableReferenceMode(true);
    // The Serializer's lifetime ends right after reference mode is switched off (the body's
    // end), where the tree called ~Serializer by hand: raw storage in `scope`, ended there.
    scope.ser = ::new (static_cast<void*>(scope.serStorage)) IO::Serializer(stream, 3);
    IO::Serializer& ser = *scope.ser;
    LoadTime::Stamp("ser.reftable", serDir);   // NOT RETAIL: print-only
    extFactory->SerializeReferenceTable(stream);
    // NOT RETAIL (HITCH_SAVE): the per-save GetReferenceId memo (object/External.h). The
    // ids are final once SerializeReferenceTable has numbered the tree; the window closes
    // where reference mode is switched off below.
    const bool refIdMemo = IsSaving(&stream);
    scope.refIdMemo = refIdMemo;
    if (refIdMemo) RtObject::ReferenceIdMemoBegin(g_verifyOn != 0);
    // --- Preload every external the save references, with reference mode OFF ---
    //
    // Object::ExternalPtr's serializer (Runtime 0x180052a50) branches on
    // ExternalFactory+0x40: with reference mode ON it reads a 4-byte reference id,
    // with it OFF it reads a bool + a filename String. A compiled .rsc stores the
    // bool+string form, so a resource whose FIRST parse happens inside this window
    // is read with the wrong shape: its nested ExternalPtrs come out NULL and every
    // field after them is shifted. The tutorial save hit this on
    // Audio/Effects/RiverEffect.rsc and WindEffect.rsc, whose SoundEffect roots are
    // referenced only by the save; Audio/Effects/Environment.rsc was already loaded
    // by the ambient components and was therefore fine. The null SoundBuffer then
    // faulted inside XAudio2, because Audio::Device::PlayPlatform derefs it as a
    // WAVEFORMATEX with no null check.
    //
    // Retail never hits this because it preloads its resources up front; our
    // standalone loads them lazily (m_resourceList is empty). SerializeReferenceTable
    // has just filled the id -> path map without loading anything, so walking that
    // map here and forcing each resource to parse BEFORE any reference is resolved
    // restores the same invariant. It is not extra work: every one of these is about
    // to be loaded anyway as the stream resolves its references.
    //
    {
        if (!IsSaving(&stream) && extFactory) {
            // B3R: GetPathForReferenceId / FindOrLoad are members (their door
            // caches and the "[PreloadRefs] SKIPPED ... unresolved" arm are gone).
            // +0x28 is the reference map's entry count (SerializeReferenceTable zeroes
            // it at entry and the insert helper bumps it per row).
            const int refCount = extFactory->_references.GetCount();
            if (refCount > 0 && refCount < (1 << 20)) {
                extFactory->EnableReferenceMode(false);
                int loaded = 0, missing = 0;
                // SerializeReferenceTable numbers the table from ONE (its write path,
                // 1800bcdf4), so the ids a save holds are 1..refCount; id 0 is never
                // in the map and starting there would skip the last resource.
                for (int id = 1; id <= refCount; ++id) {
                    void* fname[1] = { nullptr };
                    System::Filename* path = ::new (static_cast<void*>(fname)) System::Filename(
                        extFactory->GetPathForReferenceId((unsigned int)id));
                    if (!path->_data) continue;
                    const void* pl = extFactory->FindOrLoad(*path);
                    if (pl) { ++loaded; }
                    else {
                        // Report them: these fail on the lazy path too, so the
                        // preload does not create the miss - but an unnamed miss
                        // is the kind of thing that turns into a null deref later.
                        if (++missing <= 20)
                            fprintf(stderr, "[PreloadRefs] MISS id=%d \"%s\"\n", id,
                                    path->_data);
                    }
                    // System::Filename is one refcounted char* (count at str-4,
                    // block at str-8); drop our reference so this does not leak one
                    // string per entry on every load.
                    char* str = path->_data;
                    int* rc = reinterpret_cast<int*>(str - 4);  // CAST-OK: string_header
                    if (--*rc == 0) System::MemoryAllocator::MemFree(str - 8);
                }
                extFactory->EnableReferenceMode(true);
                fprintf(stderr, "[PreloadRefs] preloaded %d/%d referenced externals "
                        "(%d not found) with reference mode off\n",
                        loaded, refCount, missing);
                fflush(stderr);
            }
        }
    }
    LoadTime::Stamp("ser.core", serDir);   // NOT RETAIL: print-only (the reference preload is done)
    intFactory->ResetPersistState(stream);

    if (IsSaving(&stream)) {
        m_userInterface->SaveAllSettings();
    }

    ::Object::SerializeExternal<ClimateGenerator>(ser, m_climateConfig);   // 0x1400194a0
    ::Object::SerializeExternal<MapGenerator>(ser, m_mapConfig);           // 0x140019ec0
    ::Object::SerializeExternal<TerrainGenerator>(ser, m_terrainConfig);   // 0x14001a0c0

    ser.Serialize(m_seed, nullptr);
    ser.Serialize(m_mapSize, nullptr);
    if (ser.GetVersion() > 0)
        ser.Serialize(m_gameFlags, nullptr);
    if (ser.GetVersion() > 2)
        ser.Serialize(m_scenarioIndex, nullptr);

    m_random->Serialize(ser);
    m_saveName.Serialize(ser);
    ser.Serialize(m_scenarioCompleted, nullptr);


    // NB: a diagnostic stand-in for this call (AchIbpPeek, below) established
    // that the achievement IBP is ABSENT in the tutorial stream (present=0), so
    // retail returns early here and this IBP is NOT the one that faults. Kept
    // out of the live path — the PersistObject probe reports the same thing
    // without replacing any retail code.
    m_scenarioInstance.Object::InternalBasePtr::Serialize(ser);
    // 0x140095bef-0x140095c0e: retail's own null test, then IsDerivedFrom and a
    // null store; no test of the type id (row 1247).
    if (!m_scenarioInstance.IsNull() &&
        !::Object::IsDerivedFromNullSafe(m_scenarioInstance, Achievement::AchievementInstance::TypeId()))
        m_scenarioInstance = nullptr;

    // 0x140095c19-0x140095c26: AchievementManager::Persist, no test of the
    // singleton (row 1247).
    // Do NOT try to peek the achievement count through `ser`'s encoding:
    // AchievementManager::Persist (Runtime 0x18009e910) builds its own encoding
    // from stream+0x10 via vtable +0x18, so a peek through the serializer reads
    // a different object and returns 0xAAAAAAAA even on a save that loads
    // perfectly. That false reading cost a full detour of this investigation;
    // the PersistObject probe measures the real arguments instead.
    ::Achievement::AchievementManager::_singleton->Persist(stream);
    m_animationManager->Serialize(ser);
    m_audioScene->Serialize(ser);

    fprintf(stderr, "[GUL::Serialize] core done — subsystems...\n"); fflush(stderr);

    m_playerController->Serialize(stream);                                           // 0x1400988c0
}

void GameUpdateLoop::SerializeAllBody(IO::Stream& stream, SerializeScope& scope) {
    ::Object::ExternalFactory* extFactory = ::Object::ExternalFactory::_singleton;
    ::Object::InternalFactory* intFactory = ::Object::InternalFactory::_singleton;
    const char* const serDir = scope.serDir;
    IO::Serializer& ser = scope.Ser();
    const bool refIdMemo = scope.refIdMemo;

    LoadTime::Stamp("ser.entities", serDir);   // NOT RETAIL: print-only
    m_entityList->Serialize(stream);                                                 // 0x1400c44a0
    LoadTime::Stamp("ser.worldgeom", serDir);   // NOT RETAIL: print-only
    m_worldGeometry->Serialize(stream);                                              // 0x1400b04d0
    // 0x140095c77-0x140095c81: two arguments; 0x14008f760 never reads R8. The
    // declaration's phantom third parameter is not this block's file (row 1247).
    LoadTime::Stamp("ser.citizens", serDir);   // NOT RETAIL: print-only
    m_citizenList->Serialize(stream);                                                // 0x14008f760 (0x140095c7a: [RDI+0xf0], no test)
    LoadTime::Stamp("ser.map", serDir);   // NOT RETAIL: print-only
    m_game->Serialize(stream);                                                       // 0x14009f100
    LoadTime::Stamp("ser.rest", serDir);   // NOT RETAIL: print-only
    m_tutorial->Serialize(stream);                                                   // 0x1400f9240
    m_toolManager->Serialize(stream);                                                // 0x140102920

    // Day counter (+0x128): { int day; Time accumulated } — version-0 section.
    {
        Calendar* dc = m_dayCounter;
        IO::Serializer ser0(stream, 0);
        ser0.Serialize(dc->year, nullptr);
        dc->yearTime.Serialize(ser0);
    }

    if (ser.GetVersion() > 1)
        m_eventNotifier->Serialize(stream);          // 0x1400952c0
    m_weather->Serialize(stream);                    // 0x140093c90
    m_imageCapture->Serialize(stream);               // 0x1400a57f0
    LoadTime::Stamp("ser.postload", serDir);   // NOT RETAIL: print-only
    EntityList_PostLoad_c4730(m_entityList, &stream);
    m_citizenList->PostLoad();   // 0x14008f580 (0x140095d44: [RDI+0xf0], no test)
    // FIX rock_seat (NOT RETAIL, docs/extensions/handins/ROCKSEAT.md): the entities were read before
    // the terrain, so the rocks' meshes are seated now. Render only: nothing serialized changes.
    if (!IsSaving(&stream) && Ext::Fix().rock_seat) {
        Ext::RockSeat::SeatAll(m_entityList);
    }

    intFactory->ClearPersistState();   // 0x140095d50-0x140095d5a: no test (row 1247)
    if (refIdMemo) {   // NOT RETAIL (HITCH_SAVE): close the memo window
        long long asks = 0, hits = 0;
        const int differ = RtObject::ReferenceIdMemoEnd(&asks, &hits);
        if (g_verifyOn) StressHarness_Verify("RefIdMemo", differ == 0, differ);
        if (g_pfProfOn) ErrLine("[Stress::save] refIdMemo asks=%lld hits=%lld differ=%d\n", asks, hits, differ);
    }
    extFactory->EnableReferenceMode(false);
    ser.IO::Serializer::~Serializer();   // where the block-scoped Serializer ended

    LoadTime::Stamp("ser.done", serDir);   // NOT RETAIL: print-only
    fprintf(stderr, "[GUL::Serialize] %s DONE\n", IsSaving(&stream) ? "SAVE" : "LOAD");
    fflush(stderr);
}

// ============================================================
// 0x1400959c0 — SaveRestore (GUL vtable +0x38)
// ============================================================
// save != 0: serialize the world into a fresh CompressedMemoryStream, then
// Store the embedded MemoryStream into the SavedState. save == 0: Retrieve
// the SavedState into the stream, Open (decompress), then deserialize.
// Caller batch CB6: the body of the old `GameUpdateLoop_SaveRestore` door, moved
// into the member. Retail's Load passes a 4th argument (R9) that this body
// never reads (row 1247). The door's reader-less
// RegisterExeAddr(0x1400959c0) row went with it.
// ------------------------------------------------------------
// EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSAVE.md, SAVETAIL.md §1 / §4): the class B / C manifest at block 1's tail
// ------------------------------------------------------------
// docs/extensions/DESIGN.md §2, handins/SAVETAIL.md §1 / §4, handins/EXTSAVE.md.
// SAVE: after the last faithful Serialize, the active B / C set as a small
// chunk (src/ext/savetail.h) -- ZERO bytes while none is active, so the
// faithful image is retail's byte for byte (the resave gate compares it whole).
// LOAD: whatever follows the last faithful Serialize is read back. Retail's
// reader never looks there (SAVETAIL §1). A valid manifest locks this game's
// B / C set to exactly the saved one (Ext::LockForGameWithSet), replacing the
// requested set GameUpdateLoop::Load locked at entry. No tail: that lock
// stands. Bytes that are not a manifest (no magic): logged and ignored.
// The reload of an in-game B / C change (Ext::LockKeepsRequest, set by
// ExtensionsDialog::ReloadGame) keeps the request lock too: its manifest is the
// set the player just changed. The records of extensions turned off are dropped
// (the missing-mod paradigm); one turned on starts with empty state.
// A manifest this build cannot honour -- an unknown id, a formatVersion newer
// than ours, a damaged chunk -- is FATAL through retail's own error box
// (System::Error::SetText(.., true) + Display: the box, then exit), because the
// world was saved under rules this build does not have, and continuing would
// simulate it under different ones. EXTSAVE.md says why not a soft failure.
static void ExtSave_WriteTail(IO::CompressedMemoryStream* cms) {
    const std::vector<uint8_t> tail = Ext::SaveTail::EncodeActive();
    if (tail.empty()) return;   // faithful / no B / C active: not one byte
    const uint32_t at = cms->IO::CompressedStream::GetCurrentPos();
    cms->IO::CompressedStream::Write(static_cast<int>(tail.size()), tail.data());
    fprintf(stderr, "[ExtSave] wrote the B / C manifest: %u bytes at block-1 offset %u\n",
            static_cast<unsigned>(tail.size()), static_cast<unsigned>(at));
    fflush(stderr);
}

static void ExtSave_Fatal(const std::string& text) {
    fprintf(stderr, "[ExtSave] FATAL: %s\n", text.c_str());
    fflush(stderr);
    System::Error::SetText(text.c_str(), true);
    System::Error::Display();   // the box; _stopApplication then exits the process
}

static void ExtSave_ReadTail(GameUpdateLoop* owner, IO::CompressedMemoryStream* cms) {
    const uint32_t pos  = cms->IO::CompressedStream::GetCurrentPos();
    const uint32_t size = cms->IO::CompressedStream::GetSize();
    if (pos >= size) return;   // a retail / faithful save: the lock from Load stands

    std::vector<uint8_t> bytes(size - pos);
    const bool readOk =
        cms->IO::CompressedStream::Read(static_cast<int>(bytes.size()), bytes.data());
    std::vector<Ext::SaveTail::Record> records;
    Ext::SaveTail::Status st = readOk
        ? Ext::SaveTail::Decode(bytes.data(), bytes.size(), records)
        : Ext::SaveTail::Status::Truncated;
    if (st == Ext::SaveTail::Status::NoManifest) {
        fprintf(stderr, "[ExtSave] %u byte(s) after the last Serialize at offset %u are not a "
                "B / C manifest -- ignored\n", static_cast<unsigned>(bytes.size()), pos);
        fflush(stderr);
        return;
    }
    std::string offending;
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTHOSTS.md): extension records are the lock set; host
    // records (a shared surface's per-town state, presentation only) never reach Validate or the lock.
    std::vector<Ext::SaveTail::Record> hosts;
    std::vector<std::string> droppedKinds;
    if (st == Ext::SaveTail::Status::Ok) {
        const std::vector<Ext::SaveTail::Record> all = records;
        st = Ext::SaveTail::SplitKinds(all, records, hosts, &droppedKinds, &offending);
    }
    for (const std::string& id : droppedKinds) {
        fprintf(stderr, "[ExtSave] record %s of a kind this build does not have -- dropped\n", id.c_str());
    }
    if (st == Ext::SaveTail::Status::Ok)
        st = Ext::SaveTail::Validate(records, &offending);
    if (st != Ext::SaveTail::Status::Ok) {
        std::string text = "This save was made with game extensions this version cannot load (";
        text += Ext::SaveTail::StatusName(st);
        if (!offending.empty()) text += ": ext:" + offending;
        text += ").";
        ExtSave_Fatal(text);
        return;
    }
    if (Ext::LockKeepsRequest()) {
        // An in-game B / C change: the request lock from Load stands. Only the records of
        // extensions still active for this game are kept.
        const size_t saved = records.size();
        size_t kept = 0;
        for (size_t i = 0; i < saved; ++i) {
            const Ext::Entry* e = Ext::FindEntry(records[i].id.c_str());
            if (e != nullptr && Ext::Active(*e)) {
                records[kept] = records[i];
                ++kept;
            }
        }
        records.resize(kept);
        fprintf(stderr, "[ExtSave] manifest: an in-game B / C change, the request stands; %u of %u saved "
                "record(s) kept:", static_cast<unsigned>(kept), static_cast<unsigned>(saved));
    } else {
        std::vector<std::string> ids;
        for (const Ext::SaveTail::Record& r : records) ids.push_back(r.id);
        if (!Ext::LockForGameWithSet(owner, ids)) {   // Validate checked every id: unreachable
            ExtSave_Fatal("This save was made with game extensions this version cannot load.");
            return;
        }
        fprintf(stderr, "[ExtSave] manifest: %u B / C extension(s) locked for this game from the save:",
                static_cast<unsigned>(records.size()));
    }
    for (const Ext::SaveTail::Record& r : records)
        fprintf(stderr, " %s(v%d)", r.id.c_str(), r.formatVersion);
    fprintf(stderr, "\n");
    fflush(stderr);
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/RESTRICTEDFOODS.md): class C, each record's payload
    // is that extension's per-town state (savetail.h, StateCodec). After the whole block above, so it
    // runs on both routes.
    if (!Ext::SaveTail::ApplyState(records, &offending)) {
        ExtSave_Fatal("This save's ext:" + offending + " state is damaged.");
        return;
    }
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTHOSTS.md): the host records; never fatal (a refused
    // one leaves that host at its defaults). PlayerToolManager::Startup reopens the window from it.
    std::vector<std::string> notes;
    Ext::SaveTail::ApplyHostState(hosts, &notes);
    for (const std::string& n : notes) {
        fprintf(stderr, "[ExtSave] %s\n", n.c_str());
    }
    if (!hosts.empty()) {
        fprintf(stderr, "[ExtSave] %u host record(s) read\n", static_cast<unsigned>(hosts.size()));
    }
    fflush(stderr);
}

// NOT RETAIL (ext.background_save, docs/extensions/handins/HITCH_SAVESNAP.md): the save arm
// below from SerializeAll's body on, for the worker. The caller ran Create, the extension's
// async-compress attach, SerializeAllHead and EntityList::FlushForSerialize on the main thread.
void GameUpdateLoop_SaveRestoreBody(GameUpdateLoop* self, IO::CompressedMemoryStream* cms,
                                    Save::SavedState* savedState,
                                    GameUpdateLoop::SerializeScope& scope) {
    self->SerializeAllBody(*cms, scope);
    ExtSave_WriteTail(cms);         // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSAVE.md): zero bytes when no B / C is active
    cms->IO::CompressedMemoryStream::Close();
    savedState->Store(cms->GetMemoryStream());
}

void GameUpdateLoop::SaveRestore(bool save, Save::SavedState* savedState) {

    // ENDGAME B.3.9 IO (W2, foreign site): the REAL IO::CompressedMemoryStream
    // (0x98 bytes: the 0x68 CompressedStream base + the MemoryStream member at
    // +0x68; src/engine/io/Stream.h) built on stack storage where the door
    // built it, its members called where the doors were, and ended by a
    // qualified `T::~T()` (the destructor chain, no free) where
    // ??1CompressedMemoryStream was.
    alignas(16) uint8_t cmsStorage[sizeof(IO::CompressedMemoryStream)];
    IO::CompressedMemoryStream* const cms =
        ::new (static_cast<void*>(cmsStorage)) IO::CompressedMemoryStream();
    if (!save) {
        LoadTime::Stamp("ser.retrieve");   // NOT RETAIL: print-only (load: the saved image -> the stream)
        savedState->Retrieve(cms->GetMemoryStream());
        LoadTime::Stamp("ser.open");   // NOT RETAIL: print-only
        cms->Open();
        SerializeAll(*cms);
        ExtSave_ReadTail(this, cms);    // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSAVE.md): the manifest's load arm, see above
        cms->IO::CompressedMemoryStream::Close();
    } else {
        const double t0 = g_pfProfOn ? LoadTime::NowMs() : 0.0;   // NOT RETAIL: the HITCH_SAVE line
        cms->Create();
        // EXT SEAM (NOT RETAIL, docs/extensions/handins/HITCH_SAVE.md): ext.async_save_compress,
        // class A -- the windows are compressed on a worker, in retail's order, while the state
        // is written (src/ext/async_save_compress/).
        const bool async = Ext::Ext().async_save_compress;
        if (async) Ext::AsyncSaveCompress::Begin(*cms);
        SerializeAll(*cms);
        ExtSave_WriteTail(cms);         // EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSAVE.md): zero bytes when no B / C is active
        cms->IO::CompressedMemoryStream::Close();
        savedState->Store(cms->GetMemoryStream());
        if (g_pfProfOn) {   // NOT RETAIL (HITCH_SAVE): one line per save in a stress run
            const Ext::AsyncSaveCompress::Stats as = Ext::AsyncSaveCompress::LastStats();
            ErrLine("[Stress::save] SaveRestore %.2f ms stored=%u async=%d jobs=%d copies=%d "
                    "maxQueued=%d mainCopy=%.2f fullWait=%.2f endWait=%.2f worker=%.2f ms\n",
                    LoadTime::NowMs() - t0, savedState->_size, async ? 1 : 0,
                    async ? as.jobs : 0, async ? as.copies : 0, async ? as.maxQueued : 0,
                    async ? as.mainCopyMs : 0.0, async ? as.fullWaitMs : 0.0,
                    async ? as.endWaitMs : 0.0, async ? as.workerMs : 0.0);
        }
    }
    cms->IO::CompressedMemoryStream::~CompressedMemoryStream();
}

// ============================================================
// Load/resave roundtrip check — the serializer gate (2026-09-13)
// ============================================================
// BANISHED_SAVE_TEST=roundtrip. Called from the LOAD-FROM-SAVE branch of
// GameUpdateLoop::Load right after SaveRestore(load) and before the loaded
// SavedState is freed, i.e. before a single sim tick. It serializes the world
// again into a fresh SavedState, decompresses BOTH images through the same
// CompressedMemoryStream path the load uses, and compares the raw serialized
// bytes. Compression, the metadata block (thumbnail, timestamp) and sim drift
// are all out of the picture: a difference here is a Serialize body that does
// not round-trip its own state. Both raw images are written next to the logs
// so a difference can be diffed offline.
// ENDGAME B.3.9 IO (W2): the ?Read / ?GetSize@CompressedStream doors it
// resolved by name are the class's own members now, called non-virtually.
// EXT SEAM (NOT RETAIL, docs/extensions/handins/EXTSAVE.md): a note -- this reads the WHOLE image, GetSize() bytes, so a
// B / C manifest at the tail is part of the compare. Faithful profile: no tail
// on either side, the compare is retail's. With a B / C extension active, a
// save WITHOUT a manifest (Gate.sav) resaves WITH one: an expected DIFF.
static bool RoundtripDecompress(Save::SavedState* savedState, uint8_t** outData, uint32_t* outSize) {
    alignas(16) uint8_t cmsStorage[sizeof(IO::CompressedMemoryStream)];
    IO::CompressedMemoryStream* const cms =
        ::new (static_cast<void*>(cmsStorage)) IO::CompressedMemoryStream();
    savedState->Retrieve(cms->GetMemoryStream());
    if (!cms->Open()) { cms->IO::CompressedMemoryStream::~CompressedMemoryStream(); return false; }
    const uint32_t size = cms->IO::CompressedStream::GetSize();
    uint8_t* data = static_cast<uint8_t*>(std::malloc(size ? size : 1));   // LEFT: a CRT malloc result (the tier-I allocation shape covers MemAlloc / Allocate only: the close decides)
    const bool ok = size == 0 || cms->IO::CompressedStream::Read(static_cast<int>(size), data);
    cms->IO::CompressedMemoryStream::Close();
    cms->IO::CompressedMemoryStream::~CompressedMemoryStream();
    *outData = data; *outSize = size;
    return ok;
}

static void RoundtripDump(const char* name, const uint8_t* data, uint32_t size) {
    FILE* f = std::fopen(name, "wb");
    if (!f) return;
    std::fwrite(data, 1, size, f);
    std::fclose(f);
}

extern "C" void GameUpdateLoop_SaveRoundtripCheck(GameUpdateLoop* self, Save::SavedState* loadedState) {
    const char* e = Opt().saveTest;
    if (!e || rtcrt::StrICmp(e, "roundtrip") != 0) return;

    // The fresh image is a real Save::SavedState (B4 Y GAMEA: was a dtor-less
    // stack twin of its three fields). Its ~SavedState is the `if (data)
    // MemFree` this probe ran by hand on the success path; the error path
    // below still leaves the image allocated, as before.
    const Save::SavedState* loaded = loadedState;
    Save::SavedState fresh(loaded->_id);
    fprintf(stderr, "[Roundtrip] SaveRestore(save) into a fresh SavedState (loaded image %u bytes)\n",
            loaded->_size);
    fflush(stderr);
    self->SaveRestore(true, &fresh);

    uint8_t* a = nullptr; uint32_t an = 0;
    uint8_t* b = nullptr; uint32_t bn = 0;
    const bool okA = RoundtripDecompress(loadedState, &a, &an);
    const bool okB = RoundtripDecompress(&fresh, &b, &bn);
    if (!okA || !okB) {
        fprintf(stderr, "[Roundtrip] RESULT=ERROR decompress loaded=%d resaved=%d\n", okA, okB);
        fflush(stderr);
        fresh._data = nullptr;   // not freed on this arm (the probe's behaviour before)
        // Both raw images go back on this arm too (review 2026-11-03, F-084: clang-analyzer
        // unix.Malloc); a failed Open left its pointer null, and free(nullptr) is a no-op.
        std::free(a);
        std::free(b);
        return;
    }
    RoundtripDump("standalone_roundtrip_loaded.bin",  a, an);
    RoundtripDump("standalone_roundtrip_resaved.bin", b, bn);

    const uint32_t n = an < bn ? an : bn;
    uint32_t first = n; uint32_t diffs = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) { if (first == n) first = i; diffs++; }
    }
    const bool equal = (an == bn) && diffs == 0;
    fprintf(stderr, "[Roundtrip] RESULT=%s raw loaded=%u resaved=%u compressed loaded=%u resaved=%u"
                    " firstDiff=%u diffBytes=%u tail=%u resavedCompressedHash=%016llx\n",
            equal ? "PASS" : "DIFF", an, bn, loaded->_size, fresh._size,
            first, diffs, an > bn ? an - bn : bn - an,
            // HITCH_SAVE: the COMPRESSED resave's FNV-1a, the identity check of the
            // compression path (the gate's verdict compares the decompressed image only).
            static_cast<unsigned long long>(LoadTime::HashBytes(
                static_cast<const unsigned char*>(fresh._data), fresh._size)));
    fflush(stderr);
    std::free(a); std::free(b);
    // ~SavedState: `if (_data) MemFree(_data)`.
}
