// ============================================================================
// src/engine/save/closure_batch2b.cpp — cutover closure batch 2b
// ============================================================================
//
//   0x180032f70  Save_MetaData_CopyCtor         165 B  (unexported)
//   0x180032320  Save_SavedState_Delete          40 B  (unexported)
//
// The metadata record is the element type of the `Array<Save::MetaData>` that
// src/engine/collection/array.cpp's `kSpec_MetaData58` (0x1800332d0) grows,
// and it is reached from IO::Persistence's UpdateList (0x1800c1ea0) /
// LoadMetaData (0x1800c1b30) / SaveMetaData (0x1800c1490).
// ============================================================================

#include <atomic>
#include <cstdint>
#include <new>          // placement new into the raw destination bytes

#include "system/String.h"
#include "save/SaveLoad.h"
#include "video/Context.h"   // Rt_GuardedObjectFree (0x180006a10)

// `Array<Save::MetaDataEntry>::operator=` — retail 0x180033f30, the tail CALL at
// 0x180033048. Ported in this same commit
// (src/engine/collection/array_destroy.cpp, J4_ENTRY(180033f30)).
//
// ENDGAME B.3.2 step 2: NOT spelled as `Array<T>::operator=` on the class.
// `Collection::Array<T,...>::operator=` is still `= default` (shallow) —
// Collection.h's banner says the deep assign "becomes this class's operator=
// in step 2, when the last bitwise copier is gone", and it has not gone (this
// site's own destination is the same `Array<Save::MetaDataEntry>` header at
// d+0x48, still bare bytes with no class type here — see the banner above).
// So this calls the already-ported free function directly, by name, instead
// of by address: `RtColl_ArrayAssign_180033f30` is J4_ENTRY's forwarder onto
// `ArrayAssignCore` (array_destroy.cpp), given external linkage by sitting in
// `namespace rtcoll` there. Forward-declared here (at file scope, NOT inside
// the anonymous namespace below — that would give it internal linkage and
// this file's own copy would never bind to array_destroy.cpp's definition)
// rather than resolved through `ResolveAddr`, so the call reaches our own
// code unconditionally, with no address door and no DLL involved either way.
namespace rtcoll { void* RtColl_ArrayAssign_180033f30(void* self, const void* src); }

namespace {

void MetaEntryArrayAssign(void* dst, const void* src) {
    rtcoll::RtColl_ArrayAssign_180033f30(dst, src);
}

// The unexported `operator delete` COMDAT copy 0x180006a10 --
// Rt_GuardedObjectFree (src/engine/video/context_lifecycle.cpp), called by
// name (B3R RT-IO). Same body as ?MemFree@MemoryAllocator@System@@ and
// null-guarded inside.
void GuardedFree(void* p) {
    Rt_GuardedObjectFree(p);
}

}  // namespace

// ============================================================================
// 0x180032f70  Save::MetaData's copy constructor — 0x58 bytes.
//
// Field by field, read out of the body:
//   +0x00  qword    verbatim
//   +0x08  qword    verbatim
//   +0x10  String*  zero, adopt, INC [str-4]
//   +0x18  qword    verbatim
//   +0x20  dword    verbatim
//   +0x24  dword    verbatim
//   +0x28  qword    verbatim
//   +0x30  qword    verbatim
//   +0x38  String*  zero, adopt, INC [str-4]
//   +0x40  dword    verbatim
//   +0x44  dword    verbatim
//   +0x48  qword    ZEROED  }  the nested Array<MetaDataEntry> header, zeroed
//   +0x50  qword    ZEROED  }  and THEN assigned through 0x180033f30 — the
//                              same zero-then-operator= order as everywhere
//                              else, because operator= destroys the
//                              destination's contents first.
//   +0x48..+0x57 is the whole 16-byte Array header; nothing else is written and
//   sizeof is 0x58, so there is no unwritten tail.
//
// ENDGAME B.3.7 B2: this record IS `Save::SaveLoad::SaveInfo` — the element of
// `SaveLoad::_saves` (+0x50) that 0x1800332d0 grows — and the field meanings
// this banner used to call unknown are all pinned now (SaveLoad.h's SaveInfo
// banner). The transcription above is unchanged; it moved onto the class as the
// copy constructor, and the registered address door delegates to it.
//
// The member initialisers run in declaration order, which IS retail's store
// order above: the two Strings' zero-adopt-AddRef is ??0String@System@@QEAA@
// AEBV01@@Z (0x180006cb0), and the +0x48/+0x50 zeroing lands after +0x44 as in
// retail, ahead of the Array assign.
Save::SaveLoad::SaveInfo::SaveInfo(const SaveInfo& other)
    : thumbnail(other.thumbnail),       // +0x00 / +0x08: two qwords verbatim (pad0C too)
      description(other.description),  // +0x10
      imageBuffer(other.imageBuffer),   // +0x18
      thumbWidth(other.thumbWidth),     // +0x20
      thumbHeight(other.thumbHeight),   // +0x24
      thumbPixels(other.thumbPixels),   // +0x28
      sortKey(other.sortKey),           // +0x30
      name(other.name),                 // +0x38
      saveIndex(other.saveIndex),       // +0x40
      formatVersion(other.formatVersion),           // +0x44
      mods() {                          // +0x48 / +0x50: ZEROED, assigned below
    // 0x180033f30 — `Array<ModInfo>::operator=`, the ported J4 forwarder
    // (array_destroy.cpp), called directly by name.
    MetaEntryArrayAssign(&mods, &other.mods);
}

// (B.4 S4: the `(dst, src)` door RtSave_MetaData_CopyCtor is deleted; its one
// caller, array.cpp's kSpec_MetaData58 element op, placement-news the copy
// constructor above through rtcoll::AssignAs.)

// ============================================================================
// 0x180032320  `delete Save::SavedState`
//
// Its one caller is `Save::SaveLoad::UpdateSave` (0x1800c2d0b), on
// `SaveLoad::_saveState` — src/engine/save/SaveLoad.h already pins that
// member at +0x40 as a `SavedState*`, which is what identifies the type here.
// The call site tests the pointer itself (`TEST RCX,RCX / JZ`), so this body
// never sees null from its only caller.
//
//     if (this[1]) operator delete(this[1]);   // the heap buffer at +0x08
//     operator delete(this);
//     return this;
//
// Both frees go through 0x180006a10, which null-tests internally; the explicit
// test on +0x08 is RETAIL's, and it is kept as well. The object's own free is
// NOT guarded here for the same reason it is not there.
// ============================================================================
extern "C" void* RtSave_SavedState_Delete(Save::SavedState* self) {
    if (void* buf = self->_data) GuardedFree(buf);
    GuardedFree(self);
    return self;   // MOV RAX,<this> — retail returns the (now freed) pointer
}

