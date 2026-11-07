// src/game/Entity/componententity.cpp
// Clean reimplementation of ComponentEntity and its non-vtable methods.
//
// ComponentEntity (0x48 bytes) -- The ECS entity container.
// Vtable: 0x14015fa78, 4 slots. Extends Object::Internal.
//
// Each entity holds:
//   - An entity ID (unique identifier)
//   - Flags (24-bit mask)
//   - A description ExternalPtr (entity template)
//   - An array of Component InternalBasePtrs
//   - Aggregated entityFlags from components (update/draw/startup/shutdown bits)
//
// Decompiled from Application-x64.exe using Ghidra.
//
// ENDGAME B.3.8 K1: a REAL class (Entity/ComponentEntity.h) with a
// compiler-generated 4-slot vtable. The 12-qword `g_ComponentEntity_Standalone
// Vtable` (these 4 slots, a null, and Action's 7 — D9 #2) and both of its
// address rows are gone: nothing reads 0x14015fa78 back, and 0x14015faa0 is
// served by Action::Vtable() (ACT).

#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Component/ComponentDescription.h"
#include "Component/Component.h"
#include "Component/Description.h"
#include "options.h"
#include "platform_os.h"           // Platform::IsReadablePage (the desync trace's probe)
#include "Game/GameUpdateLoop.h"   // B3R STORAGE: GameUpdateLoop::_singleton
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "Object/factory_registration.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "object/Internal.h"   // ENDGAME B.3.3 step 6B
#include "object/TypeInfo.h"   // ENDGAME B.3.3 step 6C
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include <new>
#include "Entity/exe_bodies.h"
#include "Game/exe_bodies.h"   // Reimpl_ExternalPtr_Serialize_StringTable

// TownManager::AbortCitizenActionsFor (exe 0x140089e30) - ported in
// src/game/Component/destroycomponent_state6.cpp.
class CitizenBehavior;
extern "C" void TownMgr_AbortCitizenActionsFor_89e30(CitizenBehavior* townMgr, ComponentEntity** pEntityIbp);

extern "C" int  EntityCompValidate(const ComponentEntity*, const ComponentDescription*, Object::InternalPtr<Component>*, int);  // src/diag/entity_comp_validator.cpp

namespace {
// B6-5: the diagnostics' one-shot/limit counters are namespace-scope (were
// function-local statics in the C bodies).
std::atomic<int> s_loadLogCount{0};        // Persist: first 8 loads logged
std::atomic<int> s_noFactoryWarnings{0};   // ctor: first 32 factory misses logged
// Persist's BANISHED_SERIALIZE_TRACE stream-cursor probe state (diagnostic only).
uint32_t s_tracePrev[64];
bool     s_traceHavePrev = false;
bool     s_traceDumped   = false;

// Each component template's GetComponentType (Description slot 8, a retslot).
uint32_t TemplateComponentType(const Description* compTemplate) {
    Object::Type typeId(0);
    compTemplate->GetComponentType(&typeId);
    return typeId;
}
}  // namespace

// ============================================================
// Constructors
// ============================================================

// Default constructor (0x1400c3620): `Internal()`, the stamp (the compiler's),
// then the fields. The +0x1C handle comes out as 0x00FFFFFF: retail ORs the low
// 24 bits in and zeroes the +0x1F category byte.
ComponentEntity::ComponentEntity()
    : Object::Internal(),
      _entityList(nullptr),
      _entityId(-1),
      _handle(0x00FFFFFF),
      _lastUpdateTime(),
      _description(nullptr),
      // _components: the Array ctor zeroes data AND the full +0x38 qword (count and
      // capacity), as retail's 0x1400c3666 / 0x1400c366a.
      _entityFlags(0) {}

// Ctor from a description (0x1400c3720, retail's "CopyCtor").
ComponentEntity::ComponentEntity(ComponentDescription* description)
    : Object::Internal(),
      _entityList(nullptr),
      _entityId(-1),
      _handle(0x00FFFFFF),
      _lastUpdateTime(),
      _description(description),       // +0x28
      // _components: the Array ctor zeroes the full header (data / count / capacity,
      // 0x1400c377b / 0x1400c377f) on the MemAlloc(0x48) block, which is not zeroed.
      _entityFlags(0) {
    int descCompCount = description->_subDescCount;

    // Sanity guard: a corrupted/unexpected description (component-count field read
    // as garbage) would size the component array to an absurd value and segfault in
    // the SetSize below. Bail loudly with the desc pointer so the bad template is
    // identifiable. (Seen intermittently during resource scatter — likely heap
    // corruption upstream or an unexpected desc layout.)
    if (descCompCount < 0 || descCompCount > 256) {
        fprintf(stderr, "[CtorFromDesc] ABSURD descCompCount=%d desc=%p — entity gets 0 components\n",
                descCompCount, (void*)description);
        fflush(stderr);
        descCompCount = 0;
    }

    // First pass: count components with non-zero type IDs
    int validCount = 0;
    for (int64_t i = 0; i < (int64_t)descCompCount; i++) {
        if (TemplateComponentType(description->_subDescs[i]) != 0) validCount++;
    }

    // Allocate component array.
    // B.3.2: Collection::Array<T>::SetSize — retail 0x1400032e0, stride 8 align 8,
    // new tail zeroed. Element is the 8-byte component InternalBasePtr (byte-copy).
    _components.SetSize(validCount);

    // Second pass: create components
    int insertIdx = 0;
    for (int64_t i = 0; i < (int64_t)descCompCount; i++) {
        Description* compTemplate = description->_subDescs[i];
        uint32_t typeId = TemplateComponentType(compTemplate);
        if (typeId == 0) continue;

        // Create component from factory
        const ::Object::Factory* factory = ::Object::Factory::_singleton;
        ::Object::Base* newObj = nullptr;
        if (factory) {
            newObj = factory->CreateObject(typeId);
        }

        // Keep it only if it derives from Component: 0x1400c3829-0x1400c3840 calls
        // IsDerivedFrom on CreateObject's result (cell 0x1401c72a8) with no null
        // test, then CMOVNZ (B8 RES, row 1041: was IsDerivedFromOrTrust).
        Component* newComp = nullptr;
        if (newObj->IsDerivedFrom(Component::TypeId())) {   // cell 0x1401c72a8 (ENDGAME B.3.9 RF)
            newComp = static_cast<Component*>(newObj);
        }

        // Create IBP for new component (the owner IBP is `this`)
        ::Object::InternalPtr<Component> newIBP(newComp);

        // Resolve the component IBP (the original CopyCtor calls CheckPtr
        // before the field writes) then set owner (+0x10) and description
        // (+0x18).
        Component* compPtr = newIBP.Get();
        if (!compPtr) {
            // STANDALONE: this component typeId has no working factory (the
            // Runtime/our factory tree is incomplete — e.g. a Tree's component
            // whose Foundation-D factory is missing). The original game always
            // has every factory, so it never hits this. Skip the component
            // rather than insert a null slot that crashes the array readers
            // (RefreshEntityFlags / OnAddedPass / StartupComponents).
            if (s_noFactoryWarnings.fetch_add(1) < 32) {
                fprintf(stderr, "[CtorFromDesc] no factory for component typeId 0x%08x — skipped\n", typeId);
                fflush(stderr);
            }
            continue;
        }
        compPtr->_entity = this;
        compPtr->_description = compTemplate;

        // Store in component array
        _components[insertIdx] = newIBP;
        insertIdx++;
    }

    // Correct the array count: the SetSize above sized it to validCount, but in
    // standalone some components may have been skipped (missing factory), so the
    // real populated count is insertIdx. Leaving it at validCount would let the
    // readers walk past the last valid slot into uninitialised memory.
    _components._size = insertIdx;

    // Refresh flags and call startup on flagged components
    RefreshEntityFlags();
    StartupComponents();
}

// ============================================================
// Destructor body (0x1400c3550)
// ============================================================
// ShutdownComponents, the weak-ref invalidation, clear + free the component
// IBPs; the compiler then chains Object::Internal::~Internal. (Named "Ctor" in
// Ghidra — misleading.)
ComponentEntity::~ComponentEntity() {
    // Shutdown flagged components
    ShutdownComponents();

    // Retail's weak-ref invalidation, restored (was missing from this port).
    // 0x1400c357c-0x1400c359d: build an InternalBasePtr onto the dying entity and hand
    // it to TownManager::AbortCitizenActionsFor (0x140089e30, GameWorld+0xf0), which
    // walks every citizen's CitizenAIComponent action stack and tears down any stack
    // holding an action that references this entity (Action vtable slot 9 GetTarget ->
    // slot 8 Abort). Without it, an in-flight SendComponentMessage keeps its +0x20
    // target pointing at this block; nothing else invalidates that ref (CheckPtr is an
    // empty function in the shipping Runtime), so once the block is freed AND RECYCLED
    // every downstream probe in SendComponentMessage::AbstractImpl is defeated - the
    // pointer is canonical, the memory is readable, and the marked-for-removal bit
    // reads clear because those bytes now belong to something else. That is the crash
    // that kept moving (+0x1a4 sentinel deref -> +0x24a garbage vtable dispatch) as
    // each downstream guard was added. This is the correct layer: the refs die with
    // the entity, while it is still addressable. Same idea as the drain's
    // EntityList_ClearToolSelectionForDyingEntities, but retail-authored.
    if (GameUpdateLoop::_singleton != nullptr) {
        CitizenBehavior* townMgr = GameUpdateLoop::_singleton->m_citizenList;
        if (townMgr) {
            ComponentEntity* entityIbp = this;   // the InternalBasePtr onto the dying entity
            TownMgr_AbortCitizenActionsFor_89e30(townMgr, &entityIbp);
        }
    }

    // Iterate component IBPs: null each out, then delete old value
    int count = _components._size;
    if (count > 0) {
        for (int i = 0; i < count; i++) {
            // Save old IBP value
            ::Object::InternalPtr<Component> oldIBP = _components[i];
            // Construct null IBP in place
            _components[i] = ::Object::InternalPtr<Component>();
            // Delete old IBP (releases the component)
            oldIBP.DeletePtr();
        }
    }

    // The component array itself is freed by ~Array after this body (0x1400c35f2:
    // MemFree(+0x30), then +0x30 = 0 and the +0x38 qword = 0), before ~Internal.
}

// ============================================================
// ShutdownComponents (0x1400c3000)
// ============================================================
// If entityFlags bit 8 (0x100) is set, iterates components and calls
// vtable+0x28 (Shutdown) on each component whose Component+0x20 bit 8 is set.
void ComponentEntity::ShutdownComponents() {
    if ((_entityFlags & Bits(ComponentFlags::Shutdown)) == 0) return;

    int count = _components._size;
    if (count <= 0) return;

    for (int i = 0; i < count; i++) {
        Component* compPtr = _components[i].Get();
        // Component+0x20 is uint32 containing capability flags
        uint32_t compFlags = compPtr->_flags;
        if ((compFlags & Bits(ComponentFlags::Shutdown)) != 0) {
            // Re-check after potential lazy resolve
            compPtr = _components[i].Get();
            // Virtual slot 5 (+0x28, the shutdown slot)
            compPtr->Slot5();
        }
    }
}

// ============================================================
// StartupComponents (0x1400c30a0)
// ============================================================
// If entityFlags bit 7 (0x80) is set, iterates components and calls
// vtable+0x20 (Startup) on each component whose Component+0x20 bit 7 is set.
void ComponentEntity::StartupComponents() {
    if ((_entityFlags & Bits(ComponentFlags::Startup)) == 0) return;

    int count = _components._size;
    if (count <= 0) return;

    for (int i = 0; i < count; i++) {
        Component* compPtr = _components[i].Get();
        if (!compPtr) continue;   // defensive: skip null component (standalone factory miss)
        uint32_t compFlags = compPtr->_flags;
        if ((compFlags & Bits(ComponentFlags::Startup)) != 0) {
            compPtr = _components[i].Get();
            // Virtual slot 4 (+0x20): Startup
            compPtr->Startup();
        }
    }
}

// ============================================================
// RefreshEntityFlags (0x1400c3470)
// ============================================================
// Recomputes entityFlags by ORing in Component::+0x20 (ushort) for each
// component. If update bit (2) or draw bit (4) changed, registers entity
// in the appropriate EntityList bucket.
// Read in B6 (row 46), 0x1400c3470-0x1400c3541: the count is read once (0x1400c3486), +0x40 zeroed
// (0x1400c348d), and every slot is CheckPtr'd and its +0x20 word OR'd in (0x1400c34a0-0x1400c34c1)
// with no null test; the two appends go through 0x140003110. This member is THE body of
// 0x1400c3470 (the tree's second copy, NotifyEntityChanged in action_base_hunt.cpp, is to become a
// call to it).
void ComponentEntity::RefreshEntityFlags() {
    // [PerfX WALKCACHE -- NON-RETAIL] a component's flags changed: the update walk's
    // cached list of this entity's slot-8 components is stale (entitylist.cpp).
    EntityList_TickListInvalidate(this);

    uint32_t oldFlags = _entityFlags;

    // Reset entityFlags, then OR in each component's flags
    _entityFlags = 0;
    int count = _components._size;
    if (count > 0) {
        for (int i = 0; i < count; i++) {
            Component* compPtr = _components[i].Get();
            // Retail reads the low WORD of Component::_flags (+0x20)
            uint16_t compFlags = static_cast<uint16_t>(compPtr->_flags);
            _entityFlags |= (uint32_t)compFlags;
        }
    }

    // If entityList is set (+0x10), check for flag changes that need registration
    if (_entityList != nullptr) {
        EntityList* entityList = _entityList;

        // If update bit (bit 2) changed, register in update bucket
        if (((oldFlags ^ _entityFlags) & Bits(ComponentFlags::Tick)) != 0) {
            // EntityList +0x88 = update entity array, +0x90 = update count
            int updateCount = entityList->stagingUpdate._size + 1;
            ComponentEntity* ibp = this;   // the InternalBasePtr onto this entity
            // B.3.2: Collection::Array<T>::SetSize(n, fill) — retail 0x140003110,
            // stride 8 align 8. Element is the 8-byte entity InternalBasePtr
            // (IBPArray is Collection::Array over that row).
            entityList->stagingUpdate.SetSize(updateCount, ibp);
        }

        // If draw bit (bit 4) changed, register in draw bucket
        if (((oldFlags ^ _entityFlags) & Bits(ComponentFlags::PostUpdate)) != 0) {
            // EntityList +0x98 = draw entity array, +0xa0 = draw count
            int drawCount = entityList->stagingPostUpdate._size + 1;
            ComponentEntity* ibp = this;   // the InternalBasePtr onto this entity
            // B.3.2: Collection::Array<T>::SetSize(n, fill) — retail 0x140003110,
            // stride 8 align 8. Element is the 8-byte entity InternalBasePtr.
            entityList->stagingPostUpdate.SetSize(drawCount, ibp);
        }
    }
}

// ============================================================
// [3] Persist (0x1400c3140)
// ============================================================
// Serializes entity state: description external, component count,
// component IBPs, entity flags. On load, creates components from
// factory and inserts them into the component array.
void ComponentEntity::Persist(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);

    // Serialize description ExternalPtr at +0x28
    ::Object::SerializeExternal<ComponentDescription>(ser, _description);

    // Serialize component count
    int compCount = _components._size;
    ser.Serialize(compCount, nullptr);

    // Check if loading (encoding mode == 0)
    int encodingMode = static_cast<int>(ser.GetDirection());   // IO::Serializer::GetDirection, retail 0x180006210

    // On load: resize component array to match serialized count
    if (encodingMode == 0) {
        // B.3.2: Collection::Array<T>::SetSize — retail 0x1400032e0, stride 8 align 8,
        // new tail zeroed. Element is the 8-byte component InternalBasePtr (byte-copy).
        _components.SetSize(compCount);
        // Save/load wave diagnostics: a garbage count (stream misalignment) or
        // a failed resize (data still null with count>0) crashes the IBP loop
        // below — log the state on the way in so the crash log has context.
        if (s_loadLogCount.load() < 8 || _components._data == nullptr || static_cast<uint32_t>(compCount) > 64) {
            s_loadLogCount.fetch_add(1);
            fprintf(stderr, "[CompEnt::Serialize] LOAD ent=%p desc=%p compCount=%u "
                    "arr=%p cap=%d\n", (void*)this, (void*)_description, static_cast<uint32_t>(compCount),
                    _components._data, _components._capacity);
            fflush(stderr);
        }
    }

    // BANISHED_COMP_VALIDATE=1 — on the SAVE leg, check every component slot
    // BEFORE anything is written. A non-null slot that is not a live object is
    // what puts 0xAA-pattern objId/typeId into the stream and makes the LOAD
    // index PersistObject's id map out of bounds. Diagnostic only.
    if (encodingMode != 0)
        EntityCompValidate(this, _description, _components._data, compCount);

    // Save/load wave debug (BANISHED_SERIALIZE_TRACE): per-component progress
    // marker so a stream desync pinpoints the last-good component instead of a
    // wild crash address. Read per call (B6-5: no lazy cache).
    const char* traceOpt = Opt().serializeTrace;
    const bool trace = traceOpt && traceOpt[0] && traceOpt[0] != '0';

    // Serialize each component IBP
    int64_t cnt = (int64_t)compCount;
    if (cnt > 0) {
        for (int64_t i = 0; i < cnt; i++) {
            Object::InternalPtr<Component>* slot = &_components[i];
            if (trace) {
                fprintf(stderr, "[CompEnt::Ser] ent=%p comp %lld/%u begin raw=%p\n",
                        (void*)this, (long long)i, static_cast<uint32_t>(compCount),
                        static_cast<void*>(slot->InternalBasePtr::Get()));
                fflush(stderr);
            }
            // Serialize the InternalBasePtr
            slot->Object::InternalBasePtr::Serialize(ser);   // the base body: the Component check follows by hand
            // NOT RETAIL (F-143): the trace reads the held Internal* (no downcast to
            // Component*), since the type test below has not run yet.
            const bool rawNullAfterSer = slot->IsNull();   // before our IsDerivedFrom filter
            if (trace && rawNullAfterSer) {
                fprintf(stderr, "[CompEnt::Ser] ent=%p comp %lld RAW NULL from "
                        "IBP::Serialize (create failed -> stream desync!)\n",
                        (void*)this, (long long)i);
                fflush(stderr);
            }
            if (trace) {
                const ::Object::Internal* c = slot->InternalBasePtr::Get();
                uint32_t tid = 0;
                if (c) {
                    tid = c->Type();
                }
                // Stream cursor (CompressedMemoryStream + 0x8C — see
                // engine/types/io_serializer.h). Logged on BOTH save and load:
                // diffing the two traces per component finds an ASYMMETRIC
                // serializer (read size != write size — a silent desync that
                // corrupts every later component without crashing).
                // +0x8C is the cursor only for a CompressedMemoryStream (its
                // MemoryStream sub-object starts at +0x68); a PLAIN MemoryStream
                // keeps it at +0x24. Print both -- whichever ADVANCES is the real
                // one. A pos that never moves across components means we are
                // reading an unrelated dword, not that the stream stalled.
                uint32_t pos = 0, pos24 = 0;
                {
                    void* strm = ser._stream;
                    if (strm) {
                        // Neither documented cursor moves for THIS stream type, so
                        // find the real one instead of guessing a third offset:
                        // snapshot the first 0x100 bytes and report which dwords
                        // changed since the previous component. The cursor is the
                        // one that advances monotonically by each component's size.
                        uint32_t cur[64];
                        for (int k = 0; k < 64; ++k)
                            cur[k] = *(uint32_t*)((uint8_t*)strm + k * 4);  // CAST-OK: crash_walk
                        // The two documented cursors, read out of the same walk:
                        // CompressedMemoryStream +0x8C and MemoryStream +0x24.
                        pos   = cur[0x8C / 4];
                        pos24 = cur[0x24 / 4];
                        if (s_traceHavePrev) {
                            char buf[512]; int n = 0;
                            for (int k = 0; k < 64 && n < 400; ++k)
                                if (cur[k] != s_tracePrev[k])
                                    n += snprintf(buf + n, sizeof(buf) - n,
                                                  " +0x%02X:%u->%u", k * 4, s_tracePrev[k], cur[k]);
                            fprintf(stderr, "[CompEnt::Ser]   streamDelta%s\n",
                                    n ? buf : " (none)");
                        }
                        // One-shot full header dump. The 0xAA/0xBB values that
                        // reach PersistObject look like allocator debug fill, which
                        // would mean the reads have run off the END of the
                        // decompressed data rather than merely landing on a wrong
                        // field. Telling those two apart needs the buffer EXTENT,
                        // not another delta -- so print the raw header once and let
                        // the size field settle it against the +0x1C cursor.
                        if (!s_traceDumped) {
                            s_traceDumped = true;
                            fprintf(stderr, "[CompEnt::Ser]   streamHdr");
                            for (int k = 0; k < 40; ++k)
                                fprintf(stderr, " +0x%02X:%08X", k * 4, cur[k]);
                            fprintf(stderr, "\n");
                        }
                        // Which qword in the stream header is the DECOMPRESSED
                        // buffer? Settle it by evidence rather than by guessing an
                        // offset: at this point the previous component's record
                        // began at the previous cursor and its first dword is that
                        // component's objId, which the [Persist] trace already
                        // printed. So for each candidate base, print the dword the
                        // cursor is pointing at. The candidate that yields the
                        // NEXT objId in sequence is the buffer, and it lets the
                        // bytes at a desync be read directly instead of inferred.
                        {
                            const int cand[5] = { 0x10, 0x28, 0x30, 0x40, 0x80 };
                            uint8_t* sb = (uint8_t*)strm;  // CAST-OK: crash_walk
                            uint32_t cursor = cur[0x1C / 4];
                            fprintf(stderr, "[CompEnt::Ser]   cur=%u", cursor);
                            for (int c2 = 0; c2 < 5; ++c2) {
                                uint8_t* base = *(uint8_t**)(sb + cand[c2]);  // CAST-OK: crash_walk
                                uint32_t v = 0; bool ok = false;
                                if (base) {
                                    // VirtualQuery: committed, not PAGE_NOACCESS / PAGE_GUARD
                                    if (Platform::IsReadablePage(base + cursor)) {
                                        v = *(uint32_t*)(base + cursor); ok = true;  // CAST-OK: crash_walk
                                    }
                                }
                                fprintf(stderr, "  +0x%02X:%s%08X", cand[c2], ok ? "" : "!", v);
                            }
                            fprintf(stderr, "\n");
                        }
                        for (int k = 0; k < 64; ++k) s_tracePrev[k] = cur[k];
                        s_traceHavePrev = true;
                    }
                }
                fprintf(stderr, "[CompEnt::Ser] ent=%p comp %lld -> %p tid=%08x (%s) pos8C=%u pos24=%u\n",
                        (void*)this, (long long)i, static_cast<const void*>(c), tid,
                        c ? (::Object::GetTypeNameById(tid) ? ::Object::GetTypeNameById(tid) : "?") : "null",
                        pos, pos24);
                fflush(stderr);
            }
            // Verify component is derived from Component base; null out if not
            // NOT RETAIL (F-143): null and type tests on the held Internal*, before any
            // downcast to Component*; the answer is the same.
            if (!slot->IsNull()) {
                {
                    bool isDerived = ::Object::IsDerivedFromNullSafe(*slot, Component::TypeId());   // cell 0x1401c72a8
                    if (!isDerived) {
                        *slot = Object::InternalPtr<Component>();
                    }
                }
            }
        }
    }

    // Serialize entityFlags at +0x40
    // Retail 0x1400c3212 LEA RDX,[R15+0x40]; 0x1400c321e CALL [0x140152a50] = Serialize(unsigned int&):
    // the field's own type (known_port_issues row 820, fixed in B6).
    ser.Serialize(_entityFlags, nullptr);

    // On load: create missing components from description
    if (encodingMode == 0) {
        ComponentDescription* desc = _description;
        if (desc != nullptr) {
            // Description: +0x28 = component desc array (array of ptrs), +0x30 = count
            int descCompCount = desc->_subDescCount;
            int insertIdx = 0;
            int64_t insertPos = 0;

            for (int64_t descIdx = 0; descIdx < (int64_t)descCompCount; descIdx++) {
                Description* compTemplate = desc->_subDescs[descIdx];

                // Call vtable+0x40 on component template to get type ID
                uint32_t typeId = TemplateComponentType(compTemplate);

                if (typeId == 0) {
                    continue;
                }

                // Check if existing component at insertPos already matches
                if (insertIdx < _components._size) {
                    Component* existComp = _components[insertPos].Get();
                    // Get existing component's type ID via vtable+0x10
                    uint32_t existTypeId = existComp->Type();
                    if (existTypeId == typeId) {
                        // Already have this component -- skip
                        insertIdx++;
                        insertPos++;
                        continue;
                    }
                }

                // Create new component from factory
                const ::Object::Factory* factory = ::Object::Factory::_singleton;
                ::Object::Base* newObj = nullptr;
                if (factory) {
                    newObj = factory->CreateObject(typeId);
                }

                // Verify it's a Component
                Component* newComp = nullptr;
                if (newObj) {
                    if (::Object::IsDerivedFromNullSafe(newObj, Component::TypeId())) {   // cell 0x1401c72a8
                        newComp = static_cast<Component*>(newObj);
                    }
                }

                // Create IBP for new component
                ::Object::InternalPtr<Component> newIBP(newComp);

                // Get description template ptr for this component
                // LEFT: ComponentDescription::_subDescs is void** (COMP-c's header, running).
                Description* descTemplate = static_cast<Description*>(desc->_subDescs[descIdx]);

                // Set component fields: +0x10 = owner IBP (this entity), +0x18 = description template
                Component* compPtr = newIBP.Get();
                compPtr->_entity = this;
                compPtr->_description = descTemplate;

                // Component's virtual slot 4 (+0x20): Startup
                newIBP.Get()->Startup();

                // Resize component array to make room
                int oldCount = _components._size;
                // B.3.2: Collection::Array<T>::SetSize — retail 0x1400032e0, stride 8
                // align 8, new tail zeroed. Element is the component InternalBasePtr.
                _components.SetSize(oldCount + 1);

                // Shift existing components right to make room at insertPos
                Object::InternalPtr<Component>* compArr = _components._data;
                for (int64_t k = (int64_t)oldCount; k > insertPos; k--) {
                    compArr[k] = compArr[k - 1];
                }

                // Insert new component IBP at insertPos
                compArr[insertPos] = newIBP;

                insertIdx++;
                insertPos++;
            }

            // Refresh entity flags after loading all components
            RefreshEntityFlags();
        }
    }
}

// ============================================================
// EntityList registration (FUN_1400c48a0 + helpers) — Foundation B
// ============================================================
// In standalone these exe primitives resolve to NULL until registered, so they
// are reimplemented here in clean C++ and registered at their Ghidra addresses
// (address_registry.cpp). Used by the start-town ECS spawn.

// FUN_1400c33e0 — register an entity in its owner's update/draw buckets when the
// update(bit2)/draw(bit4) capability changed vs oldFlags. This is the same tail
// logic as RefreshEntityFlags but with an explicit oldFlags (here always 0, so a
// freshly-added entity with update/draw components gets bucketed).
//   EntityList +0x88 = update array (count +0x90), +0x98 = draw array (count +0xa0)
void ComponentEntity::RegisterUpdateDraw(uint32_t oldFlags) {
    if (_entityList == nullptr) return;
    EntityList* el = _entityList;
    uint32_t changed = oldFlags ^ _entityFlags;
    if ((changed & Bits(ComponentFlags::Tick)) != 0) {
        int updateCount = el->stagingUpdate._size + 1;
        ComponentEntity* ibp = this;   // the InternalBasePtr onto this entity
        // B.3.2: Collection::Array<T>::SetSize(n, fill) — retail 0x140003110,
        // stride 8 align 8. Element is the 8-byte entity InternalBasePtr.
        el->stagingUpdate.SetSize(updateCount, ibp);
    }
    if ((changed & Bits(ComponentFlags::PostUpdate)) != 0) {
        int drawCount = el->stagingPostUpdate._size + 1;
        ComponentEntity* ibp = this;   // the InternalBasePtr onto this entity
        // B.3.2: Collection::Array<T>::SetSize(n, fill) — retail 0x140003110,
        // stride 8 align 8. Element is the 8-byte entity InternalBasePtr.
        el->stagingPostUpdate.SetSize(drawCount, ibp);
    }
}

// FUN_1400c2f60 — "OnAdded" lifecycle pass. If entityFlags bit 0 is set, call
// each component's vtable+0x30 (register-with-systems) for components whose
// Component+0x20 bit 0 is set.
void ComponentEntity::OnAddedPass() {

    if ((_entityFlags & Bits(ComponentFlags::OnAdded)) == 0) return;
    int count = _components._size;
    for (int i = 0; i < count; i++) {
        Component* compPtr = _components[i].Get();
        if (!compPtr) continue;   // defensive: skip null component (standalone factory miss)
        const bool armed = (compPtr->_flags & Bits(ComponentFlags::OnAdded)) != 0;   // retail tests the low BYTE
        if (armed) {
            compPtr = _components[i].Get();
            compPtr->OnAdded();   // slot 6, +0x30
        }
    }
}

// EntityList::RegisterEntity (0x1400c48a0) (entityList, &entityIBP, category).
// Adds a freshly-instantiated entity to the world entity list. Offsets verified
// against the disassembly:
//   entityList+0xE0 (byte) : "closed" flag — if set, drop (delete) the entity.
//   entityList+0x08        : main entity array header {ptr@+0x08,count@+0x10,cap@+0x14}.
//                            entity id = the pre-insert main count.
//   entityList[0]          : copied to entity+0x20 (externals).
//   per-category C: bucket header at entityList+0x18+C*0x10 (count at +0x20+C*0x10).
//   entity+0x1C low-24     = bucket index (pre-insert bucket count); +0x1F = category.
//   category 6 = no bucket (entity+0x1C low-24 set to 0xFFFFFF sentinel).
// Also registers update/draw (c33e0) and runs the OnAdded pass (c2f60).
void EntityList::RegisterEntity(Object::InternalPtr<ComponentEntity>* entityIBP, int category) {
    if (shuttingDown) {                                 // list closed -> drop the entity
        // Object::InternalBasePtr::DeletePtr (0x1800c0150) on the slot: delete
        // through the virtual dtor, null the slot.
        if (ComponentEntity* dying = entityIBP->Get()) {
            delete dying;
            *entityIBP = nullptr;
        }
        return;
    }

    // Every step re-reads the slot (CheckPtr each time, matching the original).
    (*entityIBP)->_entityList = this;                   // owner = entityList
    int32_t id = masterArray._size;                     // id = current main-array count
    (*entityIBP)->_entityId = id;
    // entity+0x20 = entityList+0x00: the last-update System::Time takes the list's
    // accumulated time.
    (*entityIBP)->_lastUpdateTime._ticks = accumulatedTime._ticks;
    (*entityIBP)->RegisterUpdateDraw(0);

    // Append to the main entity array (count -> id+1).
    // B.3.2: Collection::Array<T>::SetSize(n, fill) — retail 0x140003110,
    // stride 8 align 8. Element is the 8-byte entity InternalBasePtr.
    ComponentEntity* fill = entityIBP->Get();
    masterArray.SetSize(id + 1, fill);

    // The category byte +0x1F (the handle's high byte): retail stores the BYTE.
    // ComponentEntity::_handleBytes[3] is that byte of the +0x1C dword.
    (*entityIBP)->_handleBytes[3] = (int8_t)category;
    if (category == 6) {
        (*entityIBP)->_handle |= 0xFFFFFF;               // no category bucket
    } else {
        // bucket index = the category bucket's current count
        uint32_t bucketIdx = static_cast<uint32_t>(buckets[category]._size);
        uint32_t& handle = (*entityIBP)->_handle;
        handle ^= (handle ^ bucketIdx) & 0xFFFFFF;       // low-24 = bucket index
        UpdateBucket* bucket = &buckets[category];
        int bcount = bucket->_size;
        // B.3.2: Collection::Array<T>::SetSize(n, fill) — retail 0x140003110,
        // stride 8 align 8. Element is the 8-byte entity InternalBasePtr.
        ComponentEntity* bfill = entityIBP->Get();
        bucket->SetSize(bcount + 1, bfill);
    }

    (*entityIBP)->OnAddedPass();
}

// ============================================================
// Reflection (factory cascade — concrete landmine removal)
// ============================================================
// Exe factory ComponentEntity_Factory (0x1400219c0): MemAlloc(0x48,8) ->
// ComponentEntity_Ctor (0x1400c3620) == the default ctor above. Registered as
// the real factory so Object::Factory::CreateObject("ComponentEntity") /
// External::Load no longer hits the NullFactory -> IsDerivedFrom(NULL) crash.
::Object::Base* ComponentEntity::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ComponentEntity), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ComponentEntity());
}
// Retail's in-place create is the Runtime's `Internal::CreateInPlaceObject`;
// the tree has always run the full ctor here (kept, no behaviour change).
::Object::Base* ComponentEntity::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ComponentEntity());
}

DEFINE_EXE_TYPE(ComponentEntity, "Object::Internal")
