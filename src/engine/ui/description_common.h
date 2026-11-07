#pragma once
// ============================================================================
// src/engine/ui/description_common.h — the shape every UI::*Description body
// repeats, written once.
// ============================================================================
//
// The ten classes ported alongside this header (border/button/check/clipregion/
// combo/container/documenttext/edit/graph/labeldescription.cpp) are ten
// instances of one compiler-generated shape. Five fragments appear verbatim in
// all ten, instruction for instruction:
//
//   1. the IO::Serializer PAIR on the frame and the encoding-factory call that
//      fills its second half;
//   2. the `if (rw) rw->vt[0](rw, 1)` teardown at the bottom of Serialize;
//   3. the refcounted System::String release, WITH retail's asymmetry
//      (on the last reference the FIELD is rewritten to `data - 8` and THAT is
//      freed, so the field is left holding a freed pointer; otherwise the field
//      is NULLED). Normalising the two arms would be a divergence — the same
//      asymmetry src/engine/ui/dialog_ctor_dtor.cpp documents for ~Dialog;
//   4. the `kElementFlagNeedsSizeCalc` walk up `Element::_parent` (+0x98),
//      which retail emits as a DO-WHILE: the first element is marked before
//      the null test, so a null `Element*` would fault in retail too;
//   5. attach-child-and-push: write the child's `_parent`, then
//      `Array<Element*>::SetSize(&parent->_children, size + 1, made)` — the
//      three-argument fill form, ported as array.cpp's kSpec_Fill8Z
//      (0x180076900).
//
// Writing each of the five once is not a simplification of retail: each helper
// below is a transcription of ONE fragment, and every call site names the
// retail instruction it stands for. What would have been a simplification is
// folding two DIFFERENT fragments together, and none of these does that — where
// a class's copy differs (ClipRegionDescription's Serialize has no array arm;
// ContainerDescription's applier marks TWO different elements) the difference
// stays in that class's own file.
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "ui/ElementDescription.h"   // UI::ElementDescription (B.3.6)
#include "ui/Dialog.h"               // UI::Dialog::CreateElement (fragment pass F2)
#include "ui/SpriteSheet.h"          // UI::SpriteSheet::GetSpriteIndexFromName (F2)
#include "system/MemoryAllocator.h"  // B3R RT-UI: MemAlloc / MemFree, direct
#include "system/String.h"           // B3R RT-UI: String::Serialize, direct
#include "collection/Collection.h"   // B3R RT-UI: the rtcoll bodies, direct
#include "io/Serializer.h"           // B3R RT-UI: RtIo_SerializeStringArray
#include "io/Stream.h"               // B4: IO::Stream (the Serializer's stream)
#include "ui/Element.h"              // B4: fragments 4/5 by field name
#include "ui/Sprite.h"               // B4: SpriteFromName
#include <new>

namespace rtdesc {

// ---- the IO::Serializer pair -----------------------------------------------
// [RSP+0x28] = the Stream*, [RSP+0x30] = the ReaderWriter the encoding factory
// returns, and `LEA RDX,[RSP+0x28]` is what every field serializer receives.
// Every Serialize in this family inlines `??0Serializer@IO@@` (0x1800b9960,
// version 0) and `??1Serializer@IO@@` (0x1800b99b0): B4 bodies build a real
// `IO::Serializer ser(stream, 0)` for exactly those stores and that teardown.

// B3R RT-UI: `?MemFree@MemoryAllocator@System@@` (0x1800c7ad0), called
// directly (B3R_RULES R9, correction 1).
inline void MemFree(void* p) {
    System::MemoryAllocator::MemFree(p);
}
// The locked Enter/Allocate/Leave triple every CreateInstance inlines
// (`?_singleton@MemoryAllocator@System@@` + 0x640, EnterCriticalSection,
// 0x1800c6e60, LeaveCriticalSection) has one out-of-line twin, 0x1800c7a50,
// checked instruction by instruction by src/engine/ui/factory_ui1.cpp.
inline void* MemAlloc(uint64_t bytes, int align) {
    return System::MemoryAllocator::MemAlloc(bytes, align);
}

// ---- the direct field-serializer calls ---------------------------------------
// `Object::ExternalPtr<T>::Serialize` is `Object::SerializeExternal<T>(ser, field)`
// over the typed field and the real `IO::Serializer` (External.h, B.4 P2 wave X).
// `?Serialize@String@System@@QEAAXAEAVSerializer@IO@@@Z` (0x1800c9930) is
// called by name on every description's typed System::String member (B4 Y
// RT-UI: the char** bridge had no caller left once LayoutDescription::Entry
// was typed, and is deleted).

// ---- fragment 3: the refcounted System::String release ---------------------
// See the banner: the asymmetry is retail's and is kept.
inline void ReleaseStringField(char*& s) {
    if (s != nullptr) {
        --RtSystem::StringRefCount(s);
        if (RtSystem::StringRefCount(s) == 0) {
            s = s - 8;             // the FIELD moves, then that is what is freed
            MemFree(s);
        } else {
            s = nullptr;
        }
    }
}

// ---- fragment 4: the needs-size-calc walk, as a DO-WHILE -------------------
inline void MarkNeedsSizeCalc(UI::Element* n) {
    do {
        n->_flags |= UI::kElementFlagNeedsSizeCalc;
        n = n->_parent;
    } while (n != nullptr);
}

// ---- fragment 5: attach the new child and push it into the parent ----------
inline void AttachAndPushChild(UI::Element* parent, Object::InternalPtr<UI::Element>* made) {
    UI::Element* child = made->Get();
    if (child != nullptr)
        child->_parent = parent;
    rtcoll::RtColl_ArraySetSizeFill_180076900(
        &parent->_children, parent->_children._size + 1, made);
    MarkNeedsSizeCalc(parent);
}

// ?GetSpriteIndexFromName@SpriteSheet@UI@@QEBAHAEBVString@System@@@Z, reached
// by rel32 from every one of these appliers: the `UI::SpriteSheet` member,
// called directly (SpriteFromName below).

// *** THE SPRITE INDEX IS STORED AS A QWORD READ FROM A SLOT ONLY A DWORD WAS
// WRITTEN TO. *** Every applier in this family writes `dword [RSP+n]` and then
// reads `qword [RSP+n]` and stores all eight bytes into the element. So the
// element's index+4 receives four bytes of whatever was on retail's stack. That
// is reproduced by building the sprite on the stack and COPYING it whole: its
// `pad0C` is never written (UI::Sprite's ctor leaves it), so the copy's +0x0c
// is an unwritten stack dword exactly as retail's is. Our arbitrary value is
// ours, not retail's; what is faithful is the WIDTH.
// (Identical finding, same words, in src/engine/ui/imagedescription.cpp.)
inline UI::Sprite SpriteFromName(UI::SpriteSheet* sheet, const System::String& name) {
    UI::Sprite s;
    s.spriteSheet = sheet;
    s.spriteIndex = (sheet != nullptr) ? sheet->GetSpriteIndexFromName(name) : -1;
    return s;
}

}  // namespace rtdesc
