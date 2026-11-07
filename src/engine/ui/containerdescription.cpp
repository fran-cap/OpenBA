// ============================================================================
// src/engine/ui/containerdescription.cpp — UI::ContainerDescription, whole
// ============================================================================
//
// ??_7ContainerDescription@UI@@6B@ (retail 0x180156510), ten slots:
//   0  deleting dtor      0x180039c00  src/engine/object/deleting_dtor.cpp row
//   1  TypeName           0x1800398d0  here
//   2  Type               0x1800398b0  here
//   3  GetFileVersion     0x1800ccfe0  here  (returns 0x204)
//   4  External::Compile  0x1800066e0  already ported
//   5  Serialize          0x1800cced0  here
//   6  Reconstruct        0x180039a50  here
//   7  Destruct           0x180039a80  here
//   8  CreateInstance     0x1800ccb90  here
//   9  Construct          0x180039a70  here
// plus ??1ContainerDescription@UI@@UEAA@XZ (0x1800ccff0).
//
// The constructor is OUT OF LINE (`??0ContainerDescription@UI@@QEAA@XZ`,
// 0x1800cd060) and is ALREADY ported by src/engine/ui/factory_ui1.cpp with the
// two factory steps; Construct and Reconstruct tail-jump into it.
//
// ----------------------------------------------------------------------------
// THIS IS THE ONLY CLASS IN THE BATCH WHOSE CreateInstance BUILDS **TWO**
// ELEMENTS.
// ----------------------------------------------------------------------------
// A ContainerElement (0xd8) and, nested inside it, a ClipRegionElement (0xd8) —
// the scroll viewport. The clip region gets the container's THIRD child
// description; the container itself then gets the other two. The two elements
// are kept in two registers (RSI = container, RDI = clip region) and NEITHER is
// re-read from the caller's slot, so the whole body is written with two locals.
//
// Layout: src/engine/ui/ContainerDescription.h (the real class, B.3.6
// block C4; cross-asserted against DescriptionsBatch1.h's POD). sizeof 0x90.
//
// ENDGAME B.3.6 block C4: every body below is a MEMBER of
// UI::ContainerDescription. (ENDGAME B.3.9 UI-B: the class is concrete; the
// doors and the minted table are retired.) RULE B6-5: the vtables, the type-info record and the
// ctor door the members reach are cached in namespace-scope std::atomic slots,
// never in a function-local static.
// ============================================================================
#include <new>

#include "description_common.h"
#include "ui/ContainerDescription.h"
#include "ui/ContainerElement.h"
#include "ui/ClipRegionElement.h"

// UI::ClipRegionElement is block C1's class. CreateInstance builds one inline
// (retail inlines its ctor); its own bytes are reached raw below, exactly as
// before, and the site is on the fragment list.
namespace UI { class ClipRegionElement; }

namespace {

using namespace rtdesc;

}  // namespace

// ============================================================================
// The member bodies (ENDGAME B.3.6 block C4)
// ============================================================================
namespace UI {

int32_t ContainerDescription::GetFileVersion() const { return 0x204; }

// 0x180039a70 — the ctor, placement. (Retail's `TEST RCX,RCX` sat in the
// retired door; a virtual call always has a `this`.)
Object::External* ContainerDescription::Construct() {
    return ::new (this) ContainerDescription();             // 180039a68 JMP
}
// 0x180039a50 — slot 0 with flag 0 (destroy, do not free), then the ctor. The
// slot-0 call is VIRTUAL, as in retail.
Object::External* ContainerDescription::Reconstruct() {
    this->~ContainerDescription();                                        // 180039a5e
    return ::new (this) ContainerDescription();             // 180039a68 JMP
}
// 0x180039a80
void ContainerDescription::Destruct() { this->~ContainerDescription(); }

// ============================================================================
// ?Serialize@ContainerDescription@UI@@UEAA_NAEAVStream@IO@@@Z   0x1800cced0
// ============================================================================
// THE FIELD ORDER IS NOT THE STRUCT ORDER, and it is taken from this body, not
// from the layout: three ints (+0x68, +0x6c, +0x74), then the three child
// descriptions, and only THEN the two bools at +0x70/+0x71 — which sit between
// the ints in memory. Sorting them into offset order would change the byte
// stream a .rsc parses.
bool ContainerDescription::Serialize(IO::Stream& streamv) {
    IO::Stream* stream = &streamv;

    UI::ElementDescription::Serialize(*stream);              // 1800cceea (qualified)

    IO::Serializer ser(*stream, 0);  // ??0Serializer (0x1800b9960) inlined
    IO::ReaderWriter* rw = ser._readerWriter;

    rw->SerializeInt(_requestedWidth, nullptr);             // 1800ccf2e
    rw->SerializeInt(_requestedHeight, nullptr);            // 1800ccf43
    rw->SerializeInt(_barSize, nullptr);                    // 1800ccf58

    Object::SerializeExternal<UI::ElementDescription>(ser, _partDescs[0]);   // 1800ccf5e
    Object::SerializeExternal<UI::ElementDescription>(ser, _partDescs[1]);   // 1800ccf6c
    Object::SerializeExternal<UI::ElementDescription>(ser, _partDescs[2]);   // 1800ccf7d

    rw->SerializeBool(_allowShrinkWidth, nullptr);          // 1800ccf9d
    rw->SerializeBool(_allowShrinkHeight, nullptr);         // 1800ccfaf

    // ~Serializer (0x1800b99b0) inlined here: rw's scalar deleting dtor                 // 1800ccfc5
    return true;
}

// ============================================================================
// ?CreateInstance@ContainerDescription@UI@@...   retail 0x1800ccb90
// ============================================================================
// RCX = this (R14), RDX = the hidden return slot (R12), R8 = Dialog* (R15).
//
// The order retail builds things in, which is the order here:
//
//  1. ContainerElement (0xd8): ??0Element, vtable, +0xc8 dword 0,
//     +0xcc **WORD** 0 (`MOV word ptr`, two bytes, not four), +0xd0 dword 0.
//  2. slot = container ; ElementDescription::CreateInstance(self, &slot)
//  3. ClipRegionElement (0xd8), its constructor inlined exactly as in
//     src/engine/ui/clipregiondescription.cpp, including the two UNALIGNED
//     qwords at +0xc4 and +0xcc.
//  4. child = CreateElement(dialog, &slot, _partDescs[2]) attached to the
//     CLIP REGION, not the container.
//  5. clip->ComputeSize() TWICE. Two identical virtual calls in a row; kept.
//  6. the size negotiation (see below)
//  7. the container's own five fields are copied from the description
//  8. slot = clip ; clip->_parent = container ; push clip into container
//  9. two more children from _partDescs[1] and _partDescs[0], in that order —
//     DESCENDING, and after the clip region, so the final child order is
//     [clip, desc1, desc0].
//
// STEP 6, TRANSCRIBED LITERALLY because its shape is not obvious:
//     w = clip->+0xc8
//     h = _requestedHeight ; if (h <= 0) h = clip->+0xcc
//     t = _requestedWidth  ; if (t >  0) w = t
//     if (clip->+0xc0 != w || clip->+0xc4 != h) { store both; mark the chain }
// i.e. the description's extents override the clip region's measured ones only
// when positive, and the mark is skipped when nothing changed.
//
// The ClipRegionElement's OWN fields (+0xc0..+0xd4) belong to block C1's
// header; they stay raw offsets here (fragment list).
Object::InternalPtr<Element>* ContainerDescription::CreateInstance(
        Object::InternalPtr<Element>* ret, const Dialog* dialog) const {
    // ---- 1. the container ---------------------------------------------------
    // Retail inlines ??0ContainerElement (0x1800ccc06..ccc25): the real ctor.
    void* rawC = MemAlloc(0xd8, 8);                         // 1800ccbd6
    ContainerElement* container = nullptr;
    if (rawC != nullptr)                                    // 1800ccc01
        container = ::new (rawC) ContainerElement();        // 1800ccc06 .. 1800ccc25

    // ---- 2. the base applier ------------------------------------------------
    // ?CreateInstance@ElementDescription@UI@@QEBAXV?$InternalPtr@… (0x180115ad0)
    // is name-hidden by slot 8 and its body is still a dialog.cpp door, so it
    // is reached by address, as before.
    Object::InternalPtr<Element> slot(container);                                 // 1800ccc31
    UI::ElementDescription::CreateInstance(                   // 1800ccc3e
        &slot);

    // ---- 3. the clip region -------------------------------------------------
    void* rawR = MemAlloc(0xd8, 8);                         // 1800ccc60
    UI::ClipRegionElement* clip = nullptr;
    if (rawR != nullptr)                                    // 1800ccc8b
        clip = ::new (rawR) UI::ClipRegionElement();        // 1800ccc90..1800cccc0 inlined ctor (B.3.9 UI-A)
    Element* const clipElem = clip;

    // ---- 4. the clip region's child -----------------------------------------
    // ?CreateElement@Dialog@UI@@ (0x180112ed0), its RETSLOT the same local.
    Object::InternalPtr<Element>* made = &slot;
    *made = dialog->CreateElement(Object::ExternalPtr<ElementDescription>(_partDescs[2]));   // 1800cccda
    AttachAndPushChild(clip, made);                         // 1800ccce7 .. 1800ccd24

    // ---- 5. two identical virtual calls (slot 4) ----------------------------
    clipElem->ComputeSize();                                // 1800ccd2c
    clipElem->ComputeSize();                                // 1800ccd35

    // ---- 6. the size negotiation --------------------------------------------
    {
        int32_t w = clip->_innerWidth;                                  // 1800ccd38
        int32_t h = _requestedHeight;                                   // 1800ccd3e
        if (h <= 0) h = clip->_innerHeight;                             // 1800ccd46
        const int32_t t = _requestedWidth;
        if (t > 0) w = t;                                              // 1800ccd52 CMOVG
        if (clip->_reqWidth != w ||                                    // 1800ccd55
            clip->_reqHeight != h) {                                   // 1800ccd5d
            clip->_reqWidth  = w;                                      // 1800ccd65
            clip->_reqHeight = h;                                      // 1800ccd6b
            clipElem->SetNeedsSizeCalc();                               // 1800ccd80
        }
    }

    // ---- 7. the container's own fields --------------------------------------
    container->_barSize           = _barSize;               // 1800ccd96
    container->_requestedWidth    = _requestedWidth;        // 1800ccda0
    container->_requestedHeight   = _requestedHeight;       // 1800ccdaa
    container->_allowShrinkWidth  = _allowShrinkWidth;      // 1800ccdb4 BYTE
    container->_allowShrinkHeight = _allowShrinkHeight;     // 1800ccdbf BYTE

    // ---- 8. the clip region becomes the container's first child -------------
    slot = clip;                                            // 1800ccdca
    clipElem->_parent = container;                          // 1800ccdcf
    {
        rtcoll::RtColl_ArraySetSizeFill_180076900(
            &container->_children, container->_children.GetSize() + 1, &slot);  // 1800ccdea
        container->SetNeedsSizeCalc();                      // 1800ccdf2
    }

    // ---- 9. the container's two remaining children, DESCENDING --------------
    *made = dialog->CreateElement(Object::ExternalPtr<ElementDescription>(_partDescs[1]));   // 1800cce17
    AttachAndPushChild(container, made);                    // 1800cce24 .. 1800cce54

    *made = dialog->CreateElement(Object::ExternalPtr<ElementDescription>(_partDescs[0]));   // 1800cce62
    AttachAndPushChild(container, made);                    // 1800cce6f .. 1800ccea4

    *ret = Object::InternalPtr<Element>(container);             // 1800ccea6
    return ret;
}

}  // namespace UI

namespace UI {

// ??1ContainerDescription@UI@@UEAA@XZ 0x1800ccff0 -- retail's body is the two
// restamps, ~ElementDescription's +0x60 String release and the tail into
// ~External: all the compiler's chain now.
ContainerDescription::~ContainerDescription() {}

// ?CreateNewObject@ContainerDescription@UI@@SAPEAVBase@Object@@XZ 0x1800399c0 --
// `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0x90, align 8; retail's null
// branch; the out-of-line ctor. Replaces the FACTUI1 row (B39_RULES D2).
Object::Base* ContainerDescription::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ContainerDescription), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) ContainerDescription();
}
// ?CreateInPlaceObject@ContainerDescription@UI@@SAPEAVBase@Object@@PEAX@Z 0x180039a40
Object::Base* ContainerDescription::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) ContainerDescription();
}

}  // namespace UI

// ============================================================================
// ENDGAME B.3.9 TI-UI (B39 D6): UI::ContainerDescription's own type record, built at CRT init
// (head-insert into Object::BaseTypeInfo::_firstTypeInfo). Literals copied from
// the retired minted row ?_typeInfo@ContainerDescription@UI@@ (0x1801f9da0): name "ContainerDescription", parent
// "ElementDescription" (retail ?_ParentTypeName@ContainerDescription@UI@@), no file extension; factories:
// the class's own.
// ============================================================================
DEFINE_TYPE(UI::ContainerDescription, "ElementDescription", nullptr)
