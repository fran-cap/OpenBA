// src/game/Component/populationcomponent.cpp
// Clean reimplementation of PopulationComponent::Update from Application-x64.exe.
//
// The Update method handles two tasks:
//   1. Density smoothing: accumulates time at +0x60. Every 300 game-seconds,
//      calls SmoothDensity to apply a weighted 5x5 kernel over the population grid.
//   2. Habitability scanning: processes totalCells/16 cells per tick in a
//      round-robin fashion, calling ScanHabitability for each cell. After scanning,
//      clamps smoothed density values to current density.
//
// Original: 0x140054070 (256 bytes)

#include "Component/PopulationComponent.h"
#include "Component/PopulationDescription.h"
#include "Component/ComponentDescription.h"
#include "Component/HerdComponent.h"
#include "Component/HerdDescription.h"
#include "Entity/ComponentEntity.h"
#include "Game/Map.h"   // Map, MapCell
#include "Entity/CitizenBehavior.h"       // CitizenBehavior::populations (GameUpdateLoop::m_citizenList)
#include "Object/factory_registration.h"     // Object::GetTypeIdByName
#include <new>
#include "object/TypeInfo.h"
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "system/Value.h"   // ENDGAME B.3.4b System caller batch
#include "Component/exe_bodies.h"
#include "Entity/ComponentEntity.h"   // ComponentEntity(ComponentDescription*) (0x1400c3720)
#include "Entity/EntityList.h"        // EntityList::RegisterEntity (0x1400c48a0)

// The exe helpers this file calls, by retail address:
//   0x1400538c0 PopulationComponent::SmoothDensity (below)
//   0x140053b50 PopulationComponent::ScanHabitability (below)
//   0x1400c3720 ComponentEntity_CtorFromDescription (ComponentEntity copy ctor)
//   0x1400c48a0 EntityList_RegisterEntity_Impl
//   0x140002cc0 Array<T>::Remove (DynArray8_EraseByValue)
//   0x140003110 DynArray8_ResizeFill = the typed array's own SetSizeFill (kSpec_PtrZero)
//   the GameWorld cell 0x1401c3dd8 is g_pGameWorldSingleton.

// Density smoothing interval constant: 300.0f seconds
static constexpr float SMOOTH_INTERVAL_SECONDS = 300.0f;


// ============================================================
// PopulationComponent::SmoothDensity (0x1400538c0)
// Weighted 5x5 kernel smoothing pass over the population density grid.
// Self-contained: only MemAlloc/MemFree.
// (Was silently skipped when unregistered in standalone.)
// ============================================================
void PopulationComponent::SmoothDensity() {
    // Phase 1: per cell, add the description's density growth (+0x40) into the
    //          smoothed density, then clamp it down to the density.
    {
        int i = 0;
        if (0 < cells._size) {
            do {
                Cell& cell = cells[i];
                cell.smoothed = cell.smoothed + description->_densityGrowth;
                Cell& cell2 = cells[i];
                int density = cell2.density;
                if (density < cell2.smoothed) {
                    cell2.smoothed = density;
                }
                i++;
            } while (i < cells._size);
        }
    }

    // Allocate temp buffer (one int per cell).
    int* temp = nullptr;
    int n40 = cells._size;
    if (n40 != 0) {
        if (0 < n40) {
            temp = static_cast<int*>(
                System::MemoryAllocator::MemAlloc((uint64_t)n40 * 4, 4));
        }
        // The original's zero-init loop. (Its `k < 0` arm, which re-read the slot
        // through a byte difference, cannot run: k starts at 0.)
        for (int64_t k = 0; k < (int64_t)n40; k++) {
            if (temp != nullptr) {
                temp[k] = 0;
            }
        }
        // Original frees a null pointer here (no-op); preserved for fidelity.
        if (temp != nullptr) {
            System::MemoryAllocator::MemFree(nullptr);
        }
    }

    // Phase 2: 5x5 distance-weighted kernel over the density grid (dim at +0x30).
    int dim = habitatDim;
    if (0 < dim) {
        int row = 0;
        int64_t rowL = 0;
        do {
            int col = 0;
            int64_t colL = 0;
            if (0 < dim) {
                do {
                    int acc = 0;
                    int wsum = 0;
                    int ky = -2;
                    int64_t kyL = rowL - 2;
                    do {
                        int kx = -2;
                        int64_t kxL = colL - 2;
                        do {
                            if (kxL >= 0 && (kx + col) < dim && kyL >= 0 && (ky + row) < dim) {
                                const Cell& cell = cells[dim * kyL + kxL];
                                if (cell.density != 0) {
                                    int w = (0x10 - kx * kx) - ky * ky;
                                    if (-1 < w) {
                                        acc += cell.smoothed * w;
                                        wsum += w;
                                    }
                                }
                            }
                            kx++;
                            kxL++;
                        } while (kx < 3);
                        ky++;
                        kyL++;
                    } while (ky < 3);
                    if (wsum == 0) {
                        temp[dim * rowL + colL] = 0;
                    } else {
                        temp[dim * rowL + colL] = acc / wsum;
                    }
                    col++;
                    colL++;
                    dim = habitatDim;
                } while (col < dim);
            }
            row++;
            rowL++;
            dim = habitatDim;
        } while (row < dim);
    }

    // Phase 3: write the smoothed values back into smoothed (+4) and minimum (+8).
    int n40b = cells._size;
    if (0 < n40b) {
        int i = 0;
        int* t = temp;
        do {
            Cell& cell = cells[i];
            cell.smoothed = *t;
            cell.minimum = *t;
            i++;
            t++;
        } while (i < cells._size);
    }

    System::MemoryAllocator::MemFree(temp);
}

// ============================================================
// PopulationComponent::ScanHabitability (0x140053b50)
// Scans one cell's block of map tiles, OR-ing the blocking flags + counting the
// habitable tiles; writes the result density into the cell.
// ============================================================
void PopulationComponent::ScanHabitability(Map* map, int cellIndex) {
    const PopulationDescription* scan = description;
    int walkable = 0;
    int blockDim = scan->_habitatBlock;
    unsigned int orFlags = 0;
    int gridDim = habitatDim;
    int rowStart = (cellIndex / gridDim) * blockDim;
    int colStart = (cellIndex % gridDim) * blockDim;
    int rowEnd = blockDim + rowStart;

    if (rowStart < rowEnd) {
        int r = rowStart;
        do {
            if (colStart < blockDim + colStart) {
                const MapCell* tile = &map->cells.GetData()[(int64_t)(map->size * r + colStart)];
                uint64_t remaining = (uint64_t)(unsigned int)((blockDim + colStart) - colStart);
                do {
                    unsigned int bit = Bits(PathBitOf(tile->terrainType));
                    if ((bit & scan->_habitatTerrain) != 0) {
                        walkable++;
                    }
                    orFlags |= scan->_blockingTerrain & bit;
                    if (scan->_blockedByEntity && tile->blocker != nullptr) {
                        orFlags |= 1;
                    }
                    tile += 1;  // 0x30 bytes
                    remaining--;
                } while (remaining != 0);
            }
            r++;
        } while (r < rowEnd);

        if (orFlags == 0 && 0 < walkable) {
            cells[cellIndex].density = scan->_habitatDensity;
            return;
        }
    }
    cells[cellIndex].density = 0;
}

// ============================================================
// PopulationComponent::SetCellCount (0x14000b0d0, Array<Cell>::SetSize on +0x38)
// Grows (tripling on a single-element append), copies the surviving prefix,
// zero-fills the tail, frees the old block.
// ============================================================
void PopulationComponent::SetCellCount(int newCount) {
    int oldCount = cells._size;
    if (newCount == oldCount) return;

    Cell* oldBuf = cells._data;
    cells._size = newCount;
    if (cells._capacity < newCount) {
        cells._capacity = newCount;
        if (newCount == oldCount + 1 && oldCount != 0) cells._capacity = newCount * 3;
        cells._data = static_cast<Cell*>(
            System::MemoryAllocator::MemAlloc((uint64_t)cells._capacity * 0xc, 4));
    }

    int start = (oldBuf == cells._data) ? oldCount : 0;
    for (int64_t i = start; i < newCount; i++) {
        Cell* dst = cells._data + i;
        if (!dst) continue;
        if (i < oldCount) {
            const Cell* src = oldBuf + i;
            dst->density = src->density;
            dst->smoothed = src->smoothed;
            dst->minimum = src->minimum;
        } else {
            // the tree's one qword over +0 / +4, then +8 (H7: named stores)
            dst->density = 0;
            dst->smoothed = 0;
            dst->minimum = 0;
        }
    }

    if (oldBuf != cells._data) System::MemoryAllocator::MemFree(oldBuf);
}

// ============================================================
// Reimpl of FUN_140054220 — PopulationComponent::Startup (vtable slot 4).
// ============================================================
// Ghidra labels this PopulationComponent_Serialize; it isn't. It is the
// component's spawn-time body init, dispatched from
// ComponentEntity_StartupComponents while the "PopulationDeer" template entity
// is being instantiated, and it is the ONLY place wild herds come into
// existence:
//
//   +0x28 = the PopulationDescription (from the component's +0x18)
//   +0x30 = mapDim / popDesc->_habitatBlock   (habitability grid dimension)
//   +0x48 = mapDim / popDesc->_herdBlock      (herd grid dimension)
//   +0x38 = a (+0x30)^2 array of 12-byte cells {density, smoothed, min}
//   each cell scanned once, then smoothed/min seeded from density
//   then, if popDesc->_herdTemplate is set, one herd entity per cell of the
//   (+0x48)^2 grid: instantiate the template, find its HerdComponent, point it
//   back at us (+0x28) and at its cell (+0x30), roll a size in
//   [herdDesc->_minSize, herdDesc->_maxSize), seed it via Populate, register.
// The body is PopulationComponent::Startup, below.

// ============================================================
// ENDGAME B.3.8 CIT — PopulationComponent as a REAL class (PopulationComponent.h)
// ============================================================
// Before: a 48-slot hand-built PopulationComponent_LiveVtable filled at every
// create (slots 3 Serialize, 4 Startup, 6 OnAdd, 7 OnRemove, 9 Update),
// Inert_ScalarDtor in slot 0. After: the compiler's 16-slot vtable and a real
// ctor/dtor chain (B38_RULES D1, D2). Pair with a live HerdComponent: Startup
// creates the herd entities.
#include "Object/factory_registration.h"
#include "Component/PopulationComponent.h"

// Retail 0x1400544a0 (animalwave_serialize.cpp; Ghidra calls it "Shutdown").
extern "C" void PopulationComponent_Serialize_Real(PopulationComponent* self, IO::Stream& stream);

// 0x1400546d0. Component() is the CALL 0x1401188b0; the 0x14015a360 stamp is
// the compiler's; every field = 0; flags |= 0xcb.
PopulationComponent::PopulationComponent()
    : description(nullptr), habitatDim(0), _pad34{}, herdDim(0), _pad4C{},
      smoothTimer{0}, scanIndex(0), unusedSerializedInt(0) {
    OrFlagsInCtor(0xcb);
}

void PopulationComponent::Persist(IO::Stream& stream) { PopulationComponent_Serialize_Real(this, stream); }   // [3] 0x1400544a0
void PopulationComponent::Startup() {
    if (GameUpdateLoop::_singleton == nullptr) return;
    GameUpdateLoop* gw = GameUpdateLoop::_singleton;

    uint32_t tidPopDesc = PopulationDescription::TypeId();   // cell 0x1401c47c8 (D5)
    Description* descObj = _description;
    PopulationDescription* popDesc =
        (tidPopDesc && Object::IsDerivedFromNullSafe(descObj, tidPopDesc))
            ? static_cast<PopulationDescription*>(descObj) : nullptr;
    description = popDesc;
    if (!popDesc) {
        fprintf(stderr, "[Herd] PopulationComponent::Startup: self=%p has no "
                "PopulationDescription (+0x18=%p, tid=%u) — no herds will spawn\n",
                static_cast<void*>(this), static_cast<void*>(descObj), tidPopDesc);
        fflush(stderr);
        return;
    }

    Map* map = gw->m_game;
    if (!map) return;
    int habBlock  = popDesc->_habitatBlock;
    int herdBlock = popDesc->_herdBlock;
    if (habBlock <= 0 || herdBlock <= 0) return;

    int habDim = map->size / habBlock;
    habitatDim = habDim;
    herdDim = map->size / herdBlock;

    SetCellCount(habDim * habDim);

    int count = cells._size;
    for (int i = 0; i < count; i++) {
        ScanHabitability(map, i);
        Cell& cell = cells[i];
        cell.smoothed = cell.density;
        cell.minimum = cell.density;
        count = cells._size;
    }

    // Retail vtable slot 11 (0x140054210) is an empty function.
    Slot11();   // vtable +0x58, a virtual dispatch in retail

    ComponentDescription* herdTemplate = popDesc->_herdTemplate;
    int herdCells = herdDim * herdDim;
    fprintf(stderr, "[Herd] PopulationComponent::Startup: self=%p mapDim=%d "
            "habDim=%d cells=%d herdDim=%d herds=%d template=%p\n",
            static_cast<void*>(this), map->size, habDim, count, herdDim,
            herdTemplate ? herdCells : 0, static_cast<void*>(herdTemplate));
    fflush(stderr);
    if (herdTemplate == nullptr) return;

    uint32_t tidHerdDesc = HerdDescription::TypeId();  // cell 0x1401c7858 (D5)
    System::Random* rng = gw->m_random;
    EntityList* entityList = gw->m_entityList;

    int created = 0;
    for (int i = 0; i < herdCells; i++) {
        ComponentEntity* mem = static_cast<ComponentEntity*>(
            System::MemoryAllocator::MemAlloc(0x48, 8));
        ComponentEntity* ent = mem ? ::new (mem) ComponentEntity(herdTemplate)
                                   : nullptr;
        Object::InternalPtr<ComponentEntity> entRef(ent);
        if (!ent) continue;

        // FindComponent<HerdComponent> (cell 0x1401c7820); the tree's finder skipped a
        // null component slot, which a freshly built entity never has.
        HerdComponent* herd = ent->FindComponent<HerdComponent>();
        if (!herd) continue;

        herd->populationCell = i;
        herd->population = this;

        Description* hDescObj = herd->_description;
        HerdDescription* herdDesc = (tidHerdDesc && Object::IsDerivedFromNullSafe(hDescObj, tidHerdDesc))
                             ? static_cast<HerdDescription*>(hDescObj) : nullptr;
        int size = 0;
        if (herdDesc) {
            int maxSize = herdDesc->_maxSize;
            int minSize = herdDesc->_minSize;
            size = rng->Rand(static_cast<uint32_t>(maxSize - minSize)) + minSize;
        }
        herd->Populate(size);   // 0x140120f50

        {
            Object::InternalPtr<ComponentEntity> regRef = entRef;
            entityList->RegisterEntity(&regRef, herdTemplate->_category);
        }
        created++;
    }
    fprintf(stderr, "[Herd] PopulationComponent::Startup: %d/%d herd entities created\n",
            created, herdCells);
    fflush(stderr);
}
// Town-registry membership (retail slots 6/7), a pair: slot 7 is the only
// remover of what slot 6 appends.
// ============================================================
// PopulationComponent::OnAdd (vtable slot 6) — retail 0x1400541c0
// ============================================================
// Ghidra names this one "PopulationComponent_Draw"; it draws nothing. It is the
// town-registry APPEND: the component is pushed (as an InternalBasePtr) onto the
// TownManager's population array at (GameWorld+0xF0)+0xC0, whose count lives at
// +0xC8. That array is the ONLY enumeration of the world's populations — it is
// what DebugUI::Slot4 (0x1400619E0) walks to fill the "deer population" combo on
// the debug panel's Game tab, reading each element's +0x18 PopulationDescription
// for the row text. With slot 6 never installed the array stayed empty and the
// combo had zero rows.
void PopulationComponent::OnAdded() {
    if (GameUpdateLoop::_singleton == nullptr) return;
    CitizenBehavior* townMgr = GameUpdateLoop::_singleton->m_citizenList;
    if (!townMgr) return;

    Object::InternalPtr<PopulationComponent> selfRef(this);
    townMgr->populations
        .SetSizeFill(townMgr->populations._size + 1, &selfRef);

}
// ============================================================
// PopulationComponent::OnRemove (vtable slot 7) — retail 0x140054180
// ============================================================
// Ghidra names it "PopulationComponent_OnCreate"; 0x140002cc0 is the ERASE. Pairs
// with OnAdd above — without it a destroyed population entity would leave a stale
// InternalBasePtr in the town array for the combo (and anything else walking it)
// to dereference.
void PopulationComponent::OnRemove() {
    if (GameUpdateLoop::_singleton == nullptr) return;
    CitizenBehavior* townMgr = GameUpdateLoop::_singleton->m_citizenList;
    if (!townMgr) return;

    Object::InternalPtr<PopulationComponent> selfRef(this);
    townMgr->populations.Remove(selfRef);   // 0x140002cc0
}

// [9] 0x140054070. RDX is a pointer to the elapsed System::Time (EntityList
// bucket tick 0x1400c42af).
// ============================================================
// PopulationComponent::Update (0x140054070)
// ============================================================
void PopulationComponent::Update(const System::Time* timeV) {
    const System::Time* pDeltaTime = timeV;
    // Accumulate delta time into +0x60
    smoothTimer._ticks += pDeltaTime->_ticks;


    // Convert smoothing interval (300.0f seconds) to engine ticks
    System::Time smoothInterval;
    RtSystem_Time_Ctor(&smoothInterval, SMOOTH_INTERVAL_SECONDS);

    // Run SmoothDensity for each full interval accumulated
    if (smoothTimer._ticks > smoothInterval._ticks) {
        do {
            SmoothDensity();
            smoothTimer._ticks -= smoothInterval._ticks;
        } while (smoothTimer._ticks > smoothInterval._ticks);
    }

    // Scan habitability: only while the description's rescan flag (+0x48) is set
    const PopulationDescription* owner = description;
    if (owner == nullptr || !owner->_rescan) {
        return;
    }

    // Cell count is the density array's element count at +0x40 (the DynArray at
    // +0x38/+0x40/+0x44), NOT the herd grid dim at +0x48. Retail reads +0x40 for
    // both the per-tick slice size and the round-robin wrap.
    int totalCells = cells._size;
    // Signed division by 16 with rounding toward zero
    int cellsToProcess = (totalCells + ((totalCells >> 31) & 0xF)) >> 4;
    if (cellsToProcess <= 0) return;

    // Get terrain grid from game world global
    if (GameUpdateLoop::_singleton == nullptr) return;
    Map* terrainGrid = GameUpdateLoop::_singleton->m_game;

    Cell* gridData = cells._data;

    for (int i = 0; i < cellsToProcess; i++) {
        int idx = scanIndex;

        // Scan habitability
        ScanHabitability(terrainGrid, idx);

        // Clamp smoothed and minimum to density
        Cell& cellBase = gridData[idx];
        int density = cellBase.density;

        if (cellBase.smoothed > density) cellBase.smoothed = density;
        if (cellBase.minimum > density) cellBase.minimum = density;

        // Advance scan index with wrap-around
        scanIndex = scanIndex + 1;
        if (scanIndex == totalCells) scanIndex = 0;
    }
}

void PopulationComponent::Slot11() {}                             // [11] 0x140054210: retail's own `RET`

// create 0x14000af50: MemAlloc(0x70, 8), then the ctor.
::Object::Base* PopulationComponent::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PopulationComponent), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) PopulationComponent());
}
// Retail's createInPlace is `Internal::CreateInPlaceObject` (returns null); the
// tree's in-place entry built the object, and that is kept (B38 B8-13).
::Object::Base* PopulationComponent::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) PopulationComponent());
}

DEFINE_EXE_TYPE(PopulationComponent, "Component")
