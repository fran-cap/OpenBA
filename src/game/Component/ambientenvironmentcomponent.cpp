// src/game/Component/ambientenvironmentcomponent.cpp
// Clean reimplementation of AmbientEnvironmentComponent::Update (exe 0x140043310)
// plus its two audio helpers (sub_2a10 0x140042a10, sub_2c60 0x140042c60).
//
// This is the biome-driven ambient "bed" audio — the wind/nature loops that
// crossfade between a day set and a night set based on temperature, sampled over
// the tile-grid region the entity covers. It drives the sibling
// AmbientEmitterComponent on the same entity through the world Audio::Scene
// (gameWorld+0x78) using the 3D emitter API.
//
// FAITHFUL PORT (verified in Ghidra against the three exe functions):
//
//   Update(self):
//     grid   = *(g_pGameWorld+0xf8)              // Game object; +0x00 gridWidth, +0x80 cells
//     desc   = IsDerivedFrom(self+0x18, AmbientEnvironmentDescription)   // biome cfg
//     count  = desc+0x28                          // biome-table entry count
//     hist   = int[count] (zeroed)
//     for y in [0, self+0x34): for x in [0, self+0x30):
//         cell = cells[(self.gridY(+0x2c)+y)*gridWidth + (self.gridX(+0x28)+x)]  (stride 0x30)
//         bits = cell+0x29                         // biome bitmask index
//         ent  = cell+0x00 ? : cell+0x08           // cell's entity ref (IBP)
//         for k in [0, count):  bt = desc.table(+0x20)[k]  (stride 0x28)
//             if (bt+0x00 mask & (1<<bits)) && (ent filter matches bt+0x08):
//                 hist[k] += bt+0x20 (weight); break
//     dominant = argmax(hist)
//     f = clamp( (weather.temp(+0x130,+0x18) - desc+0x30) / (desc+0x34 - desc+0x30), 0, 1)
//     emitter = the entity's AmbientEmitterComponent (scan owner+0x30 list, type match)
//     SetSlot(emitter, 0, table[dominant]+0x10 /*day  sound*/, f)
//     SetSlot(emitter, 1, table[dominant]+0x18 /*night sound*/, 1 - f)
//
//   SetSlot == sub_2a10, PlayNew == sub_2c60 (see bodies below).
//
// Prerequisites confirmed present in standalone: the tile grid (Game+0x80, built
// by worldgen), the weather sim (gameWorld+0x130), and the type-id globals
// (registered by the factory table). The Audio::Scene is live and ticked in-game.
//
// Gated by BANISHED_AMBIENT_AUDIO (default ON; =0 restores the inert no-op for
// A/B). Set BANISHED_AMBIENT_TRACE=1 for per-tick diagnostics.
//

#include "Component/AmbientEnvironmentComponent.h"
#include <cstdint>
#include "Component/AmbientEnvironmentDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/AmbientEmitterDescription.h"   // B.3.8 CB1a (TypeId)
#include "Component/AmbientEmitterComponent.h"   // B.3.8 CB1a (TypeId)
#include "Entity/ComponentEntity.h"
#include <new>
#include "math/Math.h"
#include "system/Value.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "Component/exe_bodies.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "Game/Map.h"
#include "Game/WeatherSimulation.h"
#include "Game/exe_constants.h"   // kRdata_* (B3R Leftovers: the .rdata constants)
#include <cstdlib>
#include <cstdio>
#include "Object/factory_registration.h"   // Object::GetTypeIdByName
#include "system/MemoryAllocator.h"
#include "Game/cell_stamp.h"   // AMBMEMO: the cell write clock
#include "stress_harness.h"    // AMBMEMO: g_verifyOn / g_compProfOn / StressHarness_Verify
#include <unordered_map>
#include <vector>

namespace {

// Type-id values (CRC32 of the type name; populated after Engine::Startup).
uint32_t s_tidEnvDesc     = 0;  // AmbientEnvironmentDescription (0x1401c3f08)
uint32_t s_tidEmitterComp = 0;  // AmbientEmitterComponent       (0x1401c3e60)
bool s_resolved = false;

void EnsureResolved() {
    if (s_resolved) return;
    s_tidEnvDesc     = AmbientEnvironmentDescription::TypeId();
    s_tidEmitterComp = AmbientEmitterComponent::TypeId();
    // Only latch as resolved once the factory registry has frozen the ids
    // (they start at the 0xFFFFFFFF sentinel before Factory::Startup).
    if (s_tidEnvDesc != 0 && s_tidEnvDesc != 0xFFFFFFFFu)
        s_resolved = true;
}

inline float saturate(float x, float hi) { return x < 0.0f ? 0.0f : (x > hi ? hi : x); }

// Find the entity's AmbientEmitterComponent by scanning its component list.
// The tree's null and bad-id guards are its own; kept, not extended.
AmbientEmitterComponent* FindSiblingEmitter(AmbientEnvironmentComponent* self) {
    ComponentEntity* owner = self->_entity.Get();
    if (!owner) return nullptr;
    int count = owner->_components._size;
    if (!owner->_components._data || !s_tidEmitterComp) return nullptr;
    for (int i = 0; i < count; ++i) {
        Component* comp = owner->_components[i].Get();
        if (!comp) continue;
        uint32_t tid = comp->Type();   // slot 2
        if (tid == s_tidEmitterComp)
            return (::Object::IsDerivedFromNullSafe(comp, s_tidEmitterComp)) ? static_cast<AmbientEmitterComponent*>(comp) : nullptr;
    }
    return nullptr;
}

// The tile grid (the Game object at gameWorld+0xf8): +0x00 width, +0x80 cells.
inline Map* TileGrid() {
    return GameUpdateLoop::_singleton->m_game;
}

}  // namespace

// ============================================================
// AmbientEnvironmentComponent::InitRegion
// (region/position half of impl_36a0, exe 0x1400436a0)
// ============================================================
// impl_36a0(self, gridX, gridY, rw, rh):
//   self+0x28=gridX  self+0x2c=gridY  self+0x30=rw  self+0x34=rh
//   grid   = *(gameWorld+0xf8);  gridWidth = *(int*)grid
//   center = (gridY + rh/2)*gridWidth + gridX + rw/2       // center tile index
//   off    = (float)(gridWidth/2) - 0.5f                    // DAT_140172114 == 0.5f
//   pos    = ( center%gridWidth - off, center/gridWidth - off, 0 )
//   self+0x38 = pos    // the original samples terrain height into z, then
//                      // immediately overwrites z with 0 — so pos.z is always 0.
//   FUN_140042960(siblingEmitter, self+0x38)  // push pos onto the emitter (+0x28)
//   Update(self)                              // the per-frame slot already runs
//                                             // this every tick, so we skip it here
//                                             // to avoid seeding play before OnAdded.
void AmbientEnvironmentComponent::InitRegion(int gridX, int gridY, int rw, int rh) {
    EnsureResolved();

    regionX = gridX;
    regionY = gridY;
    regionWidth = rw;
    regionHeight = rh;

    if (GameUpdateLoop::_singleton == nullptr) return;
    Map* grid = TileGrid();
    if (!grid) return;
    int gridWidth = grid->size;
    if (gridWidth <= 0) return;

    long long center = (long long)(gridY + (rh >> 1)) * gridWidth + gridX + (rw >> 1);
    float off = (float)(gridWidth >> 1) - 0.5f;
    float worldX = (float)(center % gridWidth) - off;
    float worldY = (float)(center / gridWidth) - off;

    // Env component position (comp+0x38 Vector3), z forced to 0 as in the original.
    position.x = worldX;
    position.y = worldY;
    position.z = 0.0f;

    // Push the position onto the sibling AmbientEmitterComponent (+0x28 Vector3).
    // At spawn the emitter has no live handles yet (OnAdded runs at register time,
    // after this), so FUN_140042960's MoveEmitter loop is a no-op and it only
    // writes the position — which is all we do here.
    AmbientEmitterComponent* emitter = FindSiblingEmitter(this);
    if (emitter) {
        emitter->position.x = worldX;
        emitter->position.y = worldY;
        emitter->position.z = 0.0f;
    }
}

// ============================================================================
// ENDGAME B.3.8 CIT — AmbientEnvironmentComponent as a REAL class
// (AmbientEnvironmentComponent.h).
//
// Before: a 48-slot hand-built AmbientEnvironmentComponent_LiveVtable filled at
// every create (slot 3 the serializer, slot 9 the biome-ambient Update),
// Inert_ScalarDtor in slot 0. After: the compiler's 16-slot vtable and a real
// ctor/dtor chain (B38_RULES D1, D2).
// ============================================================================
#include "Object/factory_registration.h"


// 0x1400438d0. Component() is the CALL 0x1401188b0; the 0x140158b70 stamp is
// the compiler's; +0x38 = 0, +0x40 (dword) = 0; flags |= 0x88. Retail leaves
// +0x28/+0x30 unwritten; they are zeroed as the tree's factory did.
AmbientEnvironmentComponent::AmbientEnvironmentComponent()
    : regionX(0), regionY(0), regionWidth(0), regionHeight(0), position(), _pad44{} {
    OrFlagsInCtor(0x88);
}


// [9] 0x140043690 (`JMP 0x140043310`). RDX is a pointer to the elapsed
// System::Time (EntityList bucket tick 0x1400c42af).
// ============================================================
// AmbientEnvironmentComponent::Update (vtable slot [9], 0x140043310)
// ============================================================
// The component Update ABI is (self, Time*); the original reads only `self`.
// (PerfX's AMBIENT_THROTTLE -- the scan re-run every 8th update per emitter -- is gone
// since PERFXEXT, docs/extensions/handins/PERFXEXT.md §1: the slot sounds it chose late
// are serialized by AmbientEmitterComponent::Persist, and its cache was never pruned.)
//
// AMBMEMO (NOT RETAIL bookkeeping, EXACT; docs/extensions/handins/AMBMEMO.md): the
// dominant biome is a pure function of the region's cells (terrain type +0x29, and the
// blocker +0x00 / else occupant +0x08 entity's description +0x28), the region rect and
// the description's biome table. It is cached per component with CellStamp's clock
// (Game/cell_stamp.h) at scan time; while no cell of the rect has been written since,
// the scan is skipped and the cached argmax used. Everything else -- the histogram's
// MemAlloc / MemFree, the crossfade, both SetSlot calls -- runs every tick as before, in
// the same order with the same arguments. The side table is keyed by the component and
// pruned in its destructor (no field in the 0x48-byte object). The idea was re-prompted
// by the unbanished project's docs/PERFORMANCE.md (its ambience reweigh fix).
namespace {

struct AmbMemo {
    const AmbientEnvironmentDescription* desc = nullptr;
    const AmbientEnvironmentBiome* table = nullptr;
    int count = 0;
    int x = 0, y = 0, w = 0, h = 0;
    uint64_t clock = 0;       // CellStamp::Now() right after the scan (0 = never scanned)
    uint64_t histHash = 0;    // the shadow compares the whole histogram, not only the argmax
    int dominant = 0;
};
// Never destroyed: a component torn down during static destruction still finds it.
std::unordered_map<const AmbientEnvironmentComponent*, AmbMemo>& s_ambMemo =
    *new std::unordered_map<const AmbientEnvironmentComponent*, AmbMemo>();
long long s_ambHits = 0, s_ambMisses = 0, s_ambUncached = 0, s_ambVerified = 0, s_ambMismatch = 0;

// Retail's per-cell loop (0x140043310), unchanged: credit the first biome whose terrain
// mask has the cell's type bit and whose entity filter is absent / matches.
void ScanRegion(const MapCell* cells, int gridWidth, int gridX, int gridY, int rw, int rh,
                const AmbientEnvironmentBiome* table, int count, int* hist) {
    for (int y = 0; y < rh; ++y) {
        for (int x = 0; x < rw; ++x) {
            intptr_t ci = (intptr_t)(gridY + y) * gridWidth + (gridX + x);
            const MapCell* cell = cells + ci;
            uint8_t bits = static_cast<uint8_t>(cell->terrainType);

            // Cell's entity reference (cell+0x00, else cell+0x08).
            ComponentEntity* ent = cell->blocker.Get();
            if (!ent) ent = cell->occupant.Get();

            for (int k = 0; k < count; ++k) {
                const AmbientEnvironmentBiome* bt = &table[k];
                uint32_t mask = bt->terrainMask;
                if ((mask & (1u << (bits & 0x1f))) == 0) continue;
                // Entity filter: no filter, or the cell entity matches bt+0x08.
                ComponentDescription* filt = bt->entityFilter;
                bool match = (ent == nullptr) || (filt == nullptr);
                if (!match) {
                    // ent+0x28 == filter target?
                    match = (ent->_description == filt);
                }
                if (match) {
                    hist[k] += bt->weight;
                    break;
                }
            }
        }
    }
}

// Dominant biome = argmax(hist), the first maximum.
int ArgMax(const int* hist, int count) {
    int dominant = 0;
    int best = hist[0];
    for (int k = 1; k < count; ++k) {
        if (hist[k] > best) { best = hist[k]; dominant = k; }
    }
    return dominant;
}

uint64_t HashHist(const int* hist, int count) {   // FNV-1a over the counts
    uint64_t h = 0xcbf29ce484222325ull;
    for (int k = 0; k < count; ++k) { h ^= (uint32_t)hist[k]; h *= 0x100000001b3ull; }
    return h;
}

}  // namespace

static_assert(sizeof(MapCell) == CellStamp::kCellStride, "CellStamp's stride is the MapCell record");

// [0] 0x140002170: retail's dtor does no own work. AMBMEMO drops the entry so a new
// component at this address starts cold.
AmbientEnvironmentComponent::~AmbientEnvironmentComponent() {
    s_ambMemo.erase(this);
}

void AmbientEnvironmentComponent::ReportMemo() {
    if (!g_compProfOn && !g_verifyOn) return;
    if (!s_ambHits && !s_ambMisses && !s_ambUncached) return;
    std::fprintf(stderr, "[AmbMemo] hits=%lld misses=%lld uncached=%lld verified=%lld mismatches=%lld\n",
                 s_ambHits, s_ambMisses, s_ambUncached, s_ambVerified, s_ambMismatch);
}

void AmbientEnvironmentComponent::Update(const System::Time* /*time*/) {

    EnsureResolved();

    if (GameUpdateLoop::_singleton == nullptr) return;
    if (!s_tidEnvDesc || !::System::MemoryAllocator::_singleton) return;

    // Grid (Game object at gameWorld+0xf8): +0x00 width, +0x80 cells (stride 0x30).
    Map* grid = TileGrid();
    if (!grid) return;
    int gridWidth = grid->size;
    MapCell* cells = grid->cells.GetData();

    // Biome config description at +0x18.
    Description* descRaw = _description;
    AmbientEnvironmentDescription* desc = ::Object::IsDerivedFromNullSafe(descRaw, s_tidEnvDesc)
                                              ? static_cast<AmbientEnvironmentDescription*>(descRaw) : nullptr;
    if (!desc) return;
    int count = desc->_entries._size;
    AmbientEnvironmentBiome* table = desc->_entries._data;
    if (count <= 0 || !table) return;

    int gridX = regionX;
    int gridY = regionY;
    int rw    = regionWidth;
    int rh    = regionHeight;

    // The histogram's MemAlloc / MemFree pair runs on every call, memo hit or not: the
    // allocator's free lists stay exactly as retail leaves them.
    int* hist = static_cast<int*>(::System::MemoryAllocator::MemAlloc((uint64_t)count * 4, 4));
    if (!hist) return;

    // AMBMEMO hit test: the rect must lie inside the grid (else the stamps cannot vouch
    // for every cell the scan reads) and every non-cell input must be the cached one.
    const uint64_t stamp = cells ? CellStamp::RectStamp(cells, gridWidth, gridX, gridY, rw, rh)
                                 : CellStamp::kUncacheable;
    AmbMemo* memo = nullptr;
    if (stamp != CellStamp::kUncacheable) {
        AmbMemo& m = s_ambMemo[this];
        if (m.clock != 0 && stamp <= m.clock && m.desc == desc && m.table == table && m.count == count &&
            m.x == gridX && m.y == gridY && m.w == rw && m.h == rh)
            memo = &m;
    }

    int dominant;
    if (memo) {
        ++s_ambHits;
        dominant = memo->dominant;
        if (g_verifyOn) {
            // COMPPROF=4 shadow: retail's scan on a scratch histogram, compared whole.
            static std::vector<int> s_shadow;
            s_shadow.assign((size_t)count, 0);
            ScanRegion(cells, gridWidth, gridX, gridY, rw, rh, table, count, s_shadow.data());
            const int d = ArgMax(s_shadow.data(), count);
            const uint64_t hh = HashHist(s_shadow.data(), count);
            const bool ok = d == dominant && hh == memo->histHash;
            ++s_ambVerified;
            StressHarness_Verify("AmbientMemo", ok,
                                 ok ? 0 : ((long long)gridY * gridWidth + gridX) * 2 + (d != dominant ? 1 : 0));
            if (!ok) {
                ++s_ambMismatch;
                dominant = d;   // the faithful answer drives the tick; one stale region counts once
                memo->dominant = d;
                memo->histHash = hh;
            }
        }
    } else {
        for (int i = 0; i < count; ++i) hist[i] = 0;
        if (cells) ScanRegion(cells, gridWidth, gridX, gridY, rw, rh, table, count, hist);
        dominant = ArgMax(hist, count);
        if (stamp != CellStamp::kUncacheable) {
            ++s_ambMisses;
            AmbMemo& m = s_ambMemo[this];
            m.desc = desc; m.table = table; m.count = count;
            m.x = gridX; m.y = gridY; m.w = rw; m.h = rh;
            m.clock = CellStamp::Now();   // no cell is written during the scan
            m.histHash = HashHist(hist, count);
            m.dominant = dominant;
        } else {
            ++s_ambUncached;
        }
    }

    // Temperature crossfade factor: (temp - lo) / (hi - lo), clamped [0,1].
    float unit = kRdata_14017214c;
    WeatherSimulation* weather = GameUpdateLoop::_singleton->m_weather;
    float f = 0.0f;
    if (weather) {
        float temp = weather->temperature;
        float lo = desc->_f30;
        float hi = desc->_f34;
        float denom = hi - lo;
        f = saturate((denom != 0.0f) ? (temp - lo) / denom : 0.0f, unit);
    }

    // Drive the sibling AmbientEmitterComponent: slot 0 = day, slot 1 = night.
    AmbientEmitterComponent* emitter = FindSiblingEmitter(this);
    if (emitter) {
        Audio::SoundEffect* daySound   = table[dominant].daySound;
        Audio::SoundEffect* nightSound = table[dominant].nightSound;
        emitter->SetSlot(0, daySound,   f);
        emitter->SetSlot(1, nightSound, unit - f);
    }
    if (hist) ::System::MemoryAllocator::MemFree(hist);
}

// create 0x140001ee0: MemAlloc(0x48, 8), then the ctor.
::Object::Base* AmbientEnvironmentComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(AmbientEnvironmentComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) AmbientEnvironmentComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* AmbientEnvironmentComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) AmbientEnvironmentComponent());
}

DEFINE_EXE_TYPE(AmbientEnvironmentComponent, "Component")
