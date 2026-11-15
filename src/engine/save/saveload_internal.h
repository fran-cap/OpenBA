#pragma once
// ============================================================================
// src/engine/save/saveload_internal.h — helpers shared by the Save lane files
// ============================================================================
//
// WHY THIS FILE EXISTS. Wave 5 lane C put every one of these in an anonymous
// namespace inside `saveload.cpp`, which was correct while that was the only
// file in the directory. Wave 13 adds `saveload_update.cpp` (the five
// UpdateXxx state machines plus OnVideoRestart), and those bodies need the
// SAME `ZeroTime`, the SAME `ReleaseStringSlot` and the SAME singleton
// accessors.
//
// Copying them into the second file was the obvious move and is the wrong one.
// `ReleaseStringSlot` is a transcription of a refcount idiom whose two dead
// stores look like mistakes (see its comment); a second copy is a second place
// for someone to "fix" it, and the two copies would then disagree about the
// heap. One definition, included twice.
//
// The helpers here are `inline` except the two Array destructors at the bottom,
// which saveload.cpp defines. The singleton accessors read the class statics
// fresh, with no function-local cache.
// ============================================================================

#include <atomic>
#include <cstdint>
#include <cstdlib>

#include "save/SaveLoad.h"
#include "io/Stream.h"
#include "io/Persistence.h"   // SaveBlock — SetupLoadFn's return
#include "io/FileSystem.h"    // ENDGAME B.3.9 SS1: FileSystem::_singleton
#include "io/Modification.h"  // ENDGAME B.3.9 SS1: Modification::_singleton
#include "system/MemoryAllocator.h"
#include "system/string_internal.h"
#include "system/timer_internal.h"

namespace RtSaveDetail {

using RtSave::SaveLoad;
using RtSave::SavedState;
using RtSave::SaveInfo;
using RtSave::ModInfo;
using RtIo::MemoryStream;

// ---- function pointer shapes ------------------------------------------------
using MemAllocFn         = void*   (*)(uint64_t size, int align);
using MemFreeFn          = void    (*)(void* p);
using MemStreamOpenFn    = bool     (*)(MemoryStream* self, int size);
using SaveLoadVoidFn     = void     (*)(SaveLoad* self);

// ?SetupLoad@SavedState@Save@@ — SELF FIRST, HIDDEN RETURN SLOT SECOND.
//
// It lives here so that saveload_update.cpp and savedstate_setupload.cpp, which
// both import it with a `using` declaration, share one spelling of it: a second
// spelling elsewhere is how the two drift, and this particular shape is one
// where drift is a WILD WRITE rather than a compile error
// (reference_hidden_retslot_wild_write.md).
//
// The order is the MSVC member-function convention — `this` in RCX, the hidden
// return pointer in RDX — which is the OPPOSITE of the GCC/Itanium sret order
// we would get by writing the natural `SaveBlock SetupLoad(SavedState*)`. All
// three retail call sites say so register for register; 0x1800c25cc is the
// clearest: `LEA RDX,[RSP+0x28]` (slot) then `MOV RCX,RAX` (self).
using SetupLoadFn        = RtIo::SaveBlock* (*)(SavedState* self, RtIo::SaveBlock* ret);

// The allocation this lane performs: the allocator's own statics
// (B3R R9 -- the per-file wrapper calls System::MemoryAllocator directly).
inline void* MemAlloc(uint64_t size, int align) {
    return ::System::MemoryAllocator::MemAlloc(size, align);
}
inline void MemFree(void* p) {
    ::System::MemoryAllocator::MemFree(p);
}

// The SaveLoad singleton's slot: the address of the class static
// `Save::SaveLoad::_singleton`.
inline SaveLoad** SaveLoadSingletonSlot() {
    return &::Save::SaveLoad::_singleton;
}
// ENDGAME B.3.9 SS1 (B39_RULES D7): the three below are class statics, read
// fresh, no lazy cache.
inline ::IO::FileSystem* FileSystemSingleton() {
    return ::IO::FileSystem::_singleton;
}
inline ::IO::Modification* ModificationSingleton() {
    return ::IO::Modification::_singleton;
}
inline ::IO::Persistence* PersistenceSingleton() {
    return ::IO::Persistence::_singleton;
}

inline double SecondsPerTick() { return RtSystem::TimerSecondsPerTick(); }

// `System::Time(0.0f)` as the two ctors inline it:
//     XORPS XMM0,XMM0 ; DIVSD XMM0,[Timer::_secondsPerTick] ; CVTTSD2SI RAX,XMM0
// Written out rather than folded to 0 because that is what the instructions
// say. Confirmed byte-for-byte at 0x1800c2e12..0x1800c2e2e (UpdateDelete) and
// at the identical tail of all four other UpdateXxx bodies.
inline int64_t ZeroTime() {
    const double q = 0.0 / SecondsPerTick();
    return static_cast<int64_t>(q);
}

// ---- the System::String refcount idiom, transcribed -------------------------
// Retail inlines this at ~200 sites. The shape is NOT the obvious one: when the
// refcount reaches zero it rewinds the member itself by 8 and frees THAT, and
// when it does not reach zero it stores 0 into the member. Both branches are
// dead stores at every call site that overwrites the member immediately
// afterwards — but they are what the bytes do, and this project has already
// lost a day to a String destructor applied to something that was not a String
// (reference_load_wild_write_high_dword_7fc.md, Wave 4 lane H).
//
// B.4 R-IO: every Save-class String is a real `System::String` now, whose
// destructor IS this idiom; the one remaining caller releases the raw `char*`
// name of a `RtGraphics::MaterialTexture` element (saveload_draw.cpp), whose
// type is not this block's. The refcount word is read through String.h's
// header accessor. (`RetainInto` and `AdoptReturned` went with their last
// callers.)
inline void ReleaseStringSlot(char** slot) {
    char* p = *slot;
    if (p == nullptr) return;
    RtSystem::StringRefCount(p) -= 1;
    if (RtSystem::StringRefCount(p) == 0) {
        *slot = p - 8;
        MemFree(*slot);
    } else {
        *slot = nullptr;
    }
}

// ---- the two unexported Array destructors saveload.cpp transcribes ----------
// Retail CALLs both by address from other Save bodies (UpdateList state 0 and
// ~SaveLoad call 0x1800329c0; ~SaveInfo calls 0x1800331f0). saveload.cpp
// defines both; the callers inside this directory call them directly.
//   0x1800329c0  Collection::Array<SaveInfo>::~Array  (stride 0x58)
//   0x1800331f0  Collection::Array<ModInfo>::~Array   (stride 0x20)
void ArraySaveInfoDtor(Save::SaveLoad::SaveInfoArray* arr);
void ArrayModInfoDtor(Collection::Array<Save::SaveLoad::ModInfo>* arr);

}  // namespace RtSaveDetail
