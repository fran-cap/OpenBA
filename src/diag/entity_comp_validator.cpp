// src/diag/entity_comp_validator.cpp
// BANISHED_COMP_VALIDATE=1 — save-side component-slot validator.
//
// WHY: an in-game Load crashed inside Object::InternalFactory::PersistObject
// executing `idMap[objId]` with objId=0xAAA12C08 / typeId=0xF6AAAAAA -- an
// out-of-bounds index built from 0xAA-pattern uninitialised memory.
//
// CORRECTION (2026-09-04). This header used to blame the SAVE leg, on the
// stated ground that "per-component byte counts on load match the save leg
// exactly, so the stream is NOT desynced". **That measurement was wrong and the
// conclusion with it.** A proper +0x1C cursor diff over a MATCHED build pair
// (BANISHED_SERIALIZE_TRACE=1 on both legs, correlated in ONE log) showed the
// offending component writing 71 bytes on save and consuming 13 on load. The
// stream WAS desynced, on the LOAD leg, and the 0xAA ids were allocator fill
// read past the end of a short record.
//
// Root cause, fixed in 63a0f76: a vtable slot-3 (Serialize) wrapper that never
// called its *_InitFuncs resolver. PersistObject dispatches slot 3 as the very
// first call into the class on a load, so every lazily-resolved s_fn_* was
// still null and the body null-guarded away every stream call -- consuming zero
// bytes beyond the 13-byte InternalBasePtr header. Symmetric code, asymmetric
// state. See reference_slot3_wrapper_must_resolve_first and keep
// tools/slot3_resolver_audit.py clean.
//
// Do not resurrect the save-leg theory from this file. The probe below is still
// worth keeping -- an entity whose componentCount includes a slot nothing
// populated would produce a similar-looking crash -- but it never fired for the
// bug it was written for, so treat a clean run from it as no evidence either
// way about the stream.
//
// What it does: catches a save-side write from a slot that does not look like a
// live object, naming the entity, its description type and the offending index,
// plus the type of every sibling slot so the building can be identified by its
// component signature.
//
// Diagnostic only: it never modifies the slot, so the faithful save path is
// unchanged whether the probe is on or off.

#include "options.h"
#include "Object/factory_registration.h"
#include "object/TypeInfo.h"   // Object::Base::Type (B3X W0)
#include "object/VSlot.h"      // Object::CompilerSlotOf
class ComponentEntity;        // Entity/ComponentEntity.h
class ComponentDescription;   // Component/ComponentDescription.h
class Component;              // Component/Component.h
namespace Object { template <class T> class InternalPtr; }   // object/Internal.h
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "platform_os.h"   // Platform::QueryMemoryRegion (was VirtualQuery; D.4 LINUX-1)

namespace {

bool ValidateOn() {
    return Opt().compValidate;
}

bool Readable(const void* p, size_t n) {
    if (!p) return false;
    // Committed, neither no-access nor a guard page (platform_os.h).
    Platform::MemoryRegion region;
    if (!Platform::QueryMemoryRegion(p, region)) return false;
    if (!region.accessible) return false;
    const uintptr_t end = region.base + region.size;
    return reinterpret_cast<uintptr_t>(p) + n <= end;  // CAST-OK: ptr_int
}

// A live object: readable, and its vtable pointer points into a mapped image.
bool LooksLikeObject(const void* p) {
    if (!Readable(p, 0x18)) return false;
    const void* vt = *reinterpret_cast<void* const*>(p);  // CAST-OK: crash_walk
    if (!Readable(vt, 8)) return false;
    Platform::MemoryRegion region;
    if (!Platform::QueryMemoryRegion(vt, region)) return false;
    return region.image;
}

// Object::Internal keeps its serialisation id at +0x8. Valid ids are small
// non-negative indices into InternalFactory's id map (or -1 "unregistered");
// a town never holds millions of objects, so anything outside this range
// indexes PersistObject's map out of bounds on load.
constexpr int32_t kMaxSaneObjId = 1 << 24;
bool IdLooksSane(const void* obj) {
    if (!Readable(obj, 0x10)) return false;
    // Dword 2 of a probed block (Object::Internal's id when it is one).
    const int32_t id = reinterpret_cast<const int32_t*>(obj)[2];  // CAST-OK: crash_walk
    return id >= -1 && id < kMaxSaneObjId;
}

const char* TypeNameOf(const void* obj) {
    if (!LooksLikeObject(obj)) return nullptr;
    // Probe that the table word holding Object::Base::Type() (retail slot 2,
    // +0x10) is readable -- the slot is taken from the member (object/VSlot.h)
    // -- then call Type() by name.
    const uintptr_t vt = *reinterpret_cast<const uintptr_t*>(obj);  // CAST-OK: crash_walk
    const int32_t typeSlot = ::Object::CompilerSlotOf(&::Object::Base::Type);
    if (!Readable(reinterpret_cast<void*>(vt + typeSlot * sizeof(void*)), 8)) return nullptr;  // CAST-OK: crash_walk
    // The validator probes UNTRUSTED memory (a slot that may hold garbage), so
    // an empty table word is still refused before the call.
    if (!::Object::SlotAddressOf(obj, &::Object::Base::Type)) return nullptr;
    // The probed block passed both tests: ask it its type.
    return ::Object::GetTypeNameById(static_cast<const ::Object::Base*>(obj)->Type());  // CAST-OK: crash_walk
}

} // namespace

// Called from ComponentEntity::Persist on the SAVE leg, before the component
// IBP loop writes anything. Returns the number of bad slots found.
extern "C" int EntityCompValidate(const ComponentEntity* entity, const ComponentDescription* desc,
                                  Object::InternalPtr<Component>* ibpArr, int count) {
    if (!ValidateOn() || !ibpArr || count <= 0) return 0;

    // Self-test, once: prove the detector DISCRIMINATES before trusting its
    // silence. A validator that says "no bad slots" is worthless if it would
    // also stay quiet on an obviously bogus pointer.
    static bool s_selftest = false;
    if (!s_selftest) {
        s_selftest = true;
        const bool bogus = LooksLikeObject(reinterpret_cast<void*>(0xAAAAAAAAull));  // CAST-OK: ptr_int
        const bool real  = LooksLikeObject(entity);
        fprintf(stderr, "[CompValidate] self-test: bogus(0xAAAAAAAA)=%d (want 0)  "
                        "realEntity=%d (want 1) -> detector %s\n",
                (int)bogus, (int)real,
                (!bogus && real) ? "DISCRIMINATES" : "IS BROKEN — ignore its silence");
        fflush(stderr);
    }

    // Both walks below read the IBP array's raw slot words (a slot may hold junk:
    // that is what the validator hunts), probing each for readability first.
    int bad = 0;
    for (int i = 0; i < count; ++i) {
        void** slot = reinterpret_cast<void**>(  // CAST-OK: crash_walk
            reinterpret_cast<uint8_t*>(ibpArr) + static_cast<size_t>(i) * 8);  // CAST-OK: crash_walk
        if (!Readable(slot, 8)) { ++bad; continue; }
        void* obj = *slot;
        // A null slot is legal (IBP::Serialize writes "absent"); only a NON-null
        // value that is not a live object is the bug we are hunting.
        if (!obj) continue;
        // Two distinct failures: (a) not a live object at all; (b) a live
        // object whose Object::Internal id is junk. (b) is the one that bites:
        // InternalBasePtr::Serialize writes that id as the stream objId and the
        // LOAD feeds it to PersistObject as idMap[objId]. The crash under chase
        // had objId=0xAAA12C08 IDENTICAL across two saves from two towns, so it
        // is deterministic, not heap noise. The first version of this probe only
        // tested (a) and so stayed silent on exactly the case it was written for.
        if (!LooksLikeObject(obj)) { ++bad; continue; }
        if (!IdLooksSane(obj)) ++bad;
    }
    if (!bad) return 0;

    static int s_reported = 0;
    if (s_reported >= 12) return bad;
    ++s_reported;

    fprintf(stderr, "[CompValidate] BAD ENTITY ent=%p desc=%p (%s) count=%d badSlots=%d\n",
            entity, desc, TypeNameOf(desc) ? TypeNameOf(desc) : "?", count, bad);
    for (int i = 0; i < count; ++i) {
        void** slot = reinterpret_cast<void**>(  // CAST-OK: crash_walk
            reinterpret_cast<uint8_t*>(ibpArr) + static_cast<size_t>(i) * 8);  // CAST-OK: crash_walk
        void* obj = Readable(slot, 8) ? *slot : reinterpret_cast<void*>(~0ull);  // CAST-OK: ptr_int
        const char* n = obj ? TypeNameOf(obj) : "(null)";
        // Dump the two words IBP::Serialize would read off a bad pointer — those
        // are the exact numbers that end up in the stream as objId/typeId.
        if (obj && !n && Readable(obj, 0x10)) {
            fprintf(stderr, "[CompValidate]   slot %2d = %p  *** NOT AN OBJECT *** "
                    "wouldWrite objId=%08x typeId=%08x\n", i, obj,
                    reinterpret_cast<const uint32_t*>(obj)[2],  // CAST-OK: crash_walk
                    reinterpret_cast<const uint32_t*>(obj)[3]);  // CAST-OK: crash_walk
        } else if (obj && !n) {
            fprintf(stderr, "[CompValidate]   slot %2d = %p  *** NOT AN OBJECT (unreadable) ***\n",
                    i, obj);
        } else {
            const int32_t oid = (obj && Readable(obj, 0x10))
                ? reinterpret_cast<const int32_t*>(obj)[2] : 0;  // CAST-OK: crash_walk
            fprintf(stderr, "[CompValidate]   slot %2d = %p  %-28s objId=%d%s\n",
                    i, obj, n ? n : "(null)", oid,
                    (obj && !IdLooksSane(obj))
                        ? "   *** BAD OBJECT ID -> load indexes out of bounds ***" : "");
        }
    }
    fflush(stderr);
    return bad;
}
