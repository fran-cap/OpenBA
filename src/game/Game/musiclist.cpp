// src/game/Game/musiclist.cpp
// Clean reimplementation of the MusicList resource type.
//
// MusicList (0x30 bytes) — the music-track playlist resource (Game\MusicTracks.rsc).
// Extends Object::External (0x20 base). Vtable @ 0x14015ee40.
// Registered (NullFactory stub) at exe 0x14014cc00 (MenuScene_StaticInit).
//
// It is loaded by External::Load when the in-game resource preload
// (UpdateLoop::LoadResources) reads Game\MusicTracks.rsc out of the resource
// tree. In standalone it had only a NullFactory stub, so ExternalFactory::
// FindOrLoad got NULL back and crashed in IsDerivedFrom(NULL) (Runtime+0xC039A).
// This file provides the real factory + faithful Serialize, mirroring the
// Toolbar / Scenario / NameDatabase factory-cascade pattern.
//
// Layout (from factory sub_c260, dtor 0x14001c360, Serialize 0x1400a3e90):
//   +0x00 vtable (0x14015ee40)
//   +0x08..+0x1F  Object::External base
//   +0x20 entries.data   (void*, dynamic array of 0x18-byte track entries)
//   +0x28 entries.count  (int)
// (The array header is the standard {data,count,cap} shape; the resize helper
//  at exe 0x14001c3c0 manages it. cap lives at +0x2c.)
//
// Each track entry (stride 0x18):
//   +0x00 ExternalPtr (8 bytes, ref to the track's audio resource)
//   +0x08 float   +0x0c float   +0x10 int   +0x14 int
//
// Serialize (vtable +0x28, exe 0x1400a3e90): serialize count int; if loading
// (serializer version word == 0) resize the array; then per element serialize
// float@+0x08, float@+0x0c, int@+0x10, int@+0x14, ExternalPtr@+0x00 (THIS ORDER —
// serialize order is load-bearing). The +0x00 field is serialized by exe
// FUN_1400019a0 = the templated Object::ExternalPtr::Serialize (NOT String::Serialize)
// — see the per-element note below. GetFileVersion (vtable +0x18, exe 0x1400a3f80)
// = 1 (verified: `mov eax,1; ret`).
//
// ENDGAME B.3.8 block LOOP: a REAL class (src/game/Game/MusicList.h, field
// layout and retail slot map there) with a compiler-generated vtable. Gone: the
// hand-built 9-slot `g_MusicList_Vtable` (three null slots: TypeName, Compile,
// Destruct), the lazily-resolved resize pointer (a non-atomic first-call cache,
// B6-5) and the C entry points.

#include "Game/MusicList.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Game/exe_bodies.h"
#include "system/MemoryAllocator.h"   // System::MemoryAllocator::MemAlloc / MemFree
#include "collection/Collection.h"
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch B
#include "object/Internal.h" // Object::detail::InternalMemAlloc / InternalMemFree
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <new>
#include "audio/SoundEffect.h"   // B8 RT2: ::Object::SerializeExternal<Audio::SoundEffect> (the downcast needs it complete)

// Exe-internal array resize helper for the 0x18-byte track entries
// (0x14001c3c0). The exe-internal addr is not registered, so the faithful local
// resize below, with the same default-init, is what runs.
namespace { std::atomic<bool> s_resizeWarned{false}; }

// ============================================================
// Local resize fallback (standalone) — mirrors exe FUN_14001c3c0
// ============================================================
// Grows the track array and default-inits NEW elements to
// {sound null, 0.0f, 1.0f, 0, 11}.
//
// ENDGAME B.3.2 step 2: NOT converted to `Array::SetSize`, on purpose.
// The algorithm after the two guards below matches SetSizeCore arm for arm
// (compared against the re-decompiled 0x14001c3c0: early-out, size-before-cap,
// treble on single append, byte-copy prefix on a move, full-element init on the
// tail, free old on a move). What does not match is the site's two front
// guards, which have an observable effect retail does not have: a negative
// newCount prints once and leaves the array unchanged (retail stores the
// negative size and returns), and a missing allocator returns instead of
// faulting. BRIEF.md lists "an extra guard with observable effect" as a STOP.
// (B.4 GAME-b: the header fields and the elements are read by name.)
static void MusicList_ResizeEntries_Fallback(Collection::Array<MusicListTrack>& entries, int newCount) {
    if (newCount < 0) {
        if (!s_resizeWarned.exchange(true)) {
            printf("[MusicList::Resize] negative newCount=%d header=%p — stream "
                   "misaligned upstream; leaving array unchanged\n",
                   newCount, (void*)&entries);
            fflush(stdout);
        }
        return;
    }
    if (!System::MemoryAllocator::_singleton) return;

    int oldCount = entries._size;
    if (newCount == oldCount) return;

    MusicListTrack* oldData = entries._data;
    entries._size = newCount;

    if (entries._capacity < newCount) {
        entries._capacity = newCount;
        if (newCount == oldCount + 1 && oldCount != 0) {
            entries._capacity = newCount * 3;
        }
        size_t bytes = static_cast<size_t>(entries._capacity) * sizeof(MusicListTrack);
        entries._data = static_cast<MusicListTrack*>(System::MemoryAllocator::MemAlloc(bytes, 8));
    }

    int copyStart = (oldData == entries._data) ? oldCount : 0;
    MusicListTrack* dst = entries._data;

    for (int slot = copyStart; slot < newCount; ++slot) {
        MusicListTrack* elem = dst + slot;
        if (!elem) continue;
        if (slot < oldCount) {
            *elem = oldData[slot];
        } else {
            // Default-init a fresh entry (matches exe FUN_14001c3c0).
            elem->sound      = nullptr;
            elem->minStatus = 0.0f;
            elem->maxStatus = 1.0f;
            elem->minMonth = 0;
            elem->maxMonth = 11;
        }
    }

    if (oldData != entries._data && oldData != nullptr) {
        System::MemoryAllocator::MemFree(oldData);
    }
}

// ============================================================
// MusicList::MusicList — factory sub_c260 body
// ============================================================
// External() and the 0x14015ee40 stamp are the compiler's; the array member's
// ctor is the two qword zeros at +0x20 / +0x28.
MusicList::MusicList() {}

// ============================================================
// MusicList::Serialize (vtable +0x28) — faithful port of 0x1400a3e90
// ============================================================
bool MusicList::Serialize(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);

    int count = _entries.GetSize();
    ser.Serialize(count, nullptr);

    // Loading when the serializer's stream direction is 0 (IO::Serializer::
    // GetDirection, retail 0x180006210, reads the stream's +8). (The old test of
    // the serializer's stream pointer for null is gone: it holds the stream
    // reference above.)
    // FUN_14001c3c0 is unregistered; the resize is the local mirror above.
    if (static_cast<int>(ser.GetDirection()) == 0) {
        MusicList_ResizeEntries_Fallback(_entries, count);
    }

    // Per-element read. Guard on a non-null buffer: if the array wasn't
    // allocated (e.g. count==0, or a resize that couldn't allocate), don't
    // deref null. With the load flag fixed this holds whenever count>0.
    MusicListTrack* elem = _entries.GetData();
    if (count > 0 && elem) {
        for (int i = 0; i < count; ++i) {
            // ORDER IS LOAD-BEARING: float@+0x08, float@+0x0c, int@+0x10,
            // int@+0x14, then the reference @+0x00.
            ser.Serialize(elem[i].minStatus, nullptr);
            ser.Serialize(elem[i].maxStatus, nullptr);
            ser.Serialize(elem[i].minMonth, nullptr);
            ser.Serialize(elem[i].maxMonth, nullptr);
            // +0x00 is an Object::External REFERENCE (the track's audio resource),
            // NOT a String. The original element serializer (exe FUN_1400019a0) is
            // the templated ExternalPtr::Serialize (encoding/ref-mode aware: reads a
            // uint refId in reference mode, else a bool + path String) — NOT
            // System::String::Serialize. Reimpl_ExternalPtr_Serialize_StringTable(0x14000bf40) is
            // the byte-identical instantiation (same one UIDescription's element
            // ptrs use). Reading +0x00 as a bare String consumed the wrong byte
            // count and drifted the whole element loop past EOF.
            ::Object::SerializeExternal<Audio::SoundEffect>(ser, elem[i].sound);
        }
    }

    return true;
}

// ============================================================
// MusicList::~MusicList / DeletingDtor (vtable +0x00, exe 0x14001c360)
// ============================================================
// The array free is the member's `~Array` (re-decompiled 2026-09-13:
// MemFree(+0x20); +0x20 = 0; one qword over +0x28 — ArrayDtorImpl exactly; no
// element step, retail has no element loop). External::~External is the
// compiler's chain.
MusicList::~MusicList() {}

// ============================================================
// MusicList::Reconstruct (vtable +0x30, exe 0x14001c310) — dtor(0) then re-init
// ============================================================
Object::External* MusicList::Reconstruct() {
    this->~MusicList();
    return ::new (static_cast<void*>(this)) MusicList();
}

// [7] 0x140001640 — the shared body `MOV RAX,[RCX]; XOR EDX,EDX; JMP [RAX]`:
// slot 0 with flags 0 (destroy, no free).
void MusicList::Destruct() { this->~MusicList(); }

// ============================================================
// MusicList::Construct (vtable +0x40, exe 0x14001c2c0) — in-place init
// ============================================================
Object::External* MusicList::Construct() {
    return ::new (static_cast<void*>(this)) MusicList();
}

// ============================================================
// Reflection: create / createInPlace, the registration
// ============================================================
::Object::Base* MusicList::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(MusicList), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(::new (p) MusicList());
}
::Object::Base* MusicList::CreateInPlaceObject(void* memory) {
    if (!memory) return nullptr;
    return static_cast<Object::Base*>(::new (memory) MusicList());
}


DEFINE_EXE_TYPE(MusicList, "Object::External")
